#include "../Inc/osal.h"

#if OSAL_CFG_ENABLE_EVENT

#include "../Inc/osal_event.h"
#include "../Inc/osal_irq.h"
#include "../Inc/osal_mem.h"
#include "../Inc/osal_task.h"

/*
 * 说明：
 * 1. 下面这组内部前置声明只给 system/Src 内部模块互相调用。
 * 2. event.c 会复用 task.c 里“等待原因、阻塞、恢复结果”的通用机制，
 *    但这些内部接口不对外公开，因此仍放在源文件内部声明。
 */
osal_task_t *osal_task_get_current_internal(void);
bool osal_task_contains_internal(osal_task_t *task);
osal_task_priority_t osal_task_get_priority_internal(const osal_task_t *task);
osal_task_t *osal_task_get_wait_next_internal(const osal_task_t *task);
void osal_task_set_wait_next_internal(osal_task_t *task, osal_task_t *next);
bool osal_task_is_waiting_internal(const osal_task_t *task,
                                   osal_task_wait_reason_t reason,
                                   const void *object);
osal_status_t osal_task_block_current_internal(osal_task_wait_reason_t reason,
                                               void *object,
                                               uint32_t timeout_ms);
void osal_task_wake_internal(osal_task_t *task, osal_status_t resume_status);
osal_status_t osal_task_consume_wait_result_internal(osal_task_t *task,
                                                     osal_task_wait_reason_t reason,
                                                     const void *object,
                                                     bool *handled);

/*
 * 事件对象说明：
 * 1. state 表示当前事件是否处于“已触发”状态。
 * 2. auto_reset=true 表示一次成功 wait 后自动清零。
 * 3. wait_list 是当前正在等待这个事件的任务链表。
 * 4. next 仅用于活动事件对象链表管理。
 */
struct osal_event {
    bool state;
    bool auto_reset;
    osal_task_t *wait_list;
    struct osal_event *next;
};

static osal_event_t *s_event_list = NULL;

/* 函数说明：输出事件模块调试诊断信息。 */
static void osal_event_report(const char *message) {
    OSAL_DEBUG_REPORT("event", message);
}

/* 函数说明：检查事件句柄是否仍在活动链表中。 */
#if OSAL_CFG_ENABLE_DEBUG
static bool osal_event_contains(osal_event_t *evt) {
    osal_event_t *current = s_event_list;

    while (current != NULL) {
        if (current == evt) {
            return true;
        }
        current = current->next;
    }

    return false;
}
#endif

/* 函数说明：校验事件句柄是否有效。 */
static bool osal_event_validate_handle(const osal_event_t *evt) {
    if (evt == NULL) {
        return false;
    }
#if OSAL_CFG_ENABLE_DEBUG
    if (!osal_event_contains((osal_event_t *)evt)) {
        /* debug 模式下用活动链表挡住“已删除句柄继续使用”的问题。 */
        osal_event_report("API called with inactive event handle");
        return false;
    }
#endif
    return true;
}

/* 函数说明：判断任务是否已经挂在事件等待链表中。 */
static bool osal_event_wait_list_contains(osal_task_t *head, osal_task_t *task) {
    while (head != NULL) {
        if (head == task) {
            return true;
        }
        head = osal_task_get_wait_next_internal(head);
    }

    return false;
}

/* 函数说明：把任务追加到事件等待链表尾部。 */
static void osal_event_wait_list_append(osal_task_t **head, osal_task_t *task) {
    osal_task_t *current;

    if ((head == NULL) || (task == NULL)) {
        return;
    }
    if (osal_event_wait_list_contains(*head, task)) {
        return;
    }

    osal_task_set_wait_next_internal(task, NULL);
    if (*head == NULL) {
        *head = task;
        return;
    }

    current = *head;
    while (osal_task_get_wait_next_internal(current) != NULL) {
        current = osal_task_get_wait_next_internal(current);
    }
    osal_task_set_wait_next_internal(current, task);
}

/* 函数说明：把指定任务从事件等待链表中移除。 */
static void osal_event_wait_list_remove(osal_task_t **head, osal_task_t *task) {
    osal_task_t *prev = NULL;
    osal_task_t *current;

    if ((head == NULL) || (task == NULL)) {
        return;
    }

    current = *head;
    while (current != NULL) {
        osal_task_t *next = osal_task_get_wait_next_internal(current);

        if (current == task) {
            if (prev == NULL) {
                *head = next;
            } else {
                osal_task_set_wait_next_internal(prev, next);
            }
            osal_task_set_wait_next_internal(current, NULL);
            return;
        }

        prev = current;
        current = next;
    }
}

/*
 * 这里和 queue 的策略保持一致：
 * 1. 数值越小表示优先级越高。
 * 2. 只在发现“严格更高优先级”时替换 best。
 * 3. 因此同优先级任务会保持先进入等待链表的任务先被唤醒。
 */
/* 函数说明：从事件等待链表里弹出“最高优先级、同级 FIFO”的任务。 */
static osal_task_t *osal_event_wait_list_pop_best(osal_task_t **head) {
    osal_task_t *best = NULL;
    osal_task_t *best_prev = NULL;
    osal_task_t *prev = NULL;
    osal_task_t *current;

    if ((head == NULL) || (*head == NULL)) {
        return NULL;
    }

    current = *head;
    while (current != NULL) {
        if ((best == NULL) ||
            (osal_task_get_priority_internal(current) < osal_task_get_priority_internal(best))) {
            best = current;
            best_prev = prev;
        }
        prev = current;
        current = osal_task_get_wait_next_internal(current);
    }

    if (best == NULL) {
        return NULL;
    }

    if (best_prev == NULL) {
        *head = osal_task_get_wait_next_internal(best);
    } else {
        osal_task_set_wait_next_internal(best_prev, osal_task_get_wait_next_internal(best));
    }
    osal_task_set_wait_next_internal(best, NULL);
    return best;
}

/* 函数说明：唤醒一个等待当前事件的任务。 */
static void osal_event_wake_one_waiter_locked(osal_event_t *evt, osal_status_t resume_status) {
    osal_task_t *task;

    if (evt == NULL) {
        return;
    }

    task = osal_event_wait_list_pop_best(&evt->wait_list);
    if (task != NULL) {
        /*
         * 这里只负责把任务切回 READY。
         * 对正常唤醒来说，任务下次重新进入 osal_event_wait() 时会再次检查 evt->state。
         */
        osal_task_wake_internal(task, resume_status);
    }
}

/* 函数说明：批量唤醒当前事件上的所有等待任务。 */
static void osal_event_wake_all_waiters_locked(osal_event_t *evt, osal_status_t resume_status) {
    if (evt == NULL) {
        return;
    }

    while (evt->wait_list != NULL) {
        osal_task_t *task = osal_event_wait_list_pop_best(&evt->wait_list);

        if (task == NULL) {
            break;
        }
        osal_task_wake_internal(task, resume_status);
    }
}

/* 函数说明：在临界区内判断事件当前是否可立即消费。 */
static osal_status_t osal_event_try_consume_locked(osal_event_t *evt) {
    if ((evt == NULL) || !evt->state) {
        return OSAL_ERR_RESOURCE;
    }

    if (evt->auto_reset) {
        /*
         * 自动复位事件在一次 wait 成功后就清零，
         * 这样下一次 wait 必须等新的 set 才能再次成功。
         */
        evt->state = false;
    }
    return OSAL_OK;
}

/*
 * 说明：
 * 1. 返回 OSAL_ERR_BLOCKED 不表示最终失败。
 * 2. 它表示“当前任务已经进入 BLOCKED，并挂到事件等待链表上”。
 * 3. 等到事件被 set、被 delete，或者等待超时后，任务才会在后续轮次恢复执行。
 */
/* 函数说明：把当前任务挂到事件等待链表中。 */
static osal_status_t osal_event_prepare_wait_locked(osal_event_t *evt, uint32_t timeout_ms) {
    osal_task_t *task = osal_task_get_current_internal();
    osal_status_t status;

    if ((evt == NULL) || (task == NULL)) {
        osal_event_report("blocking event wait requires current task context");
        return OSAL_ERROR;
    }
    if (!osal_task_contains_internal(task)) {
        osal_event_report("blocking event wait called with inactive task");
        return OSAL_ERR_PARAM;
    }
    if (osal_task_is_waiting_internal(task, OSAL_TASK_WAIT_EVENT, evt)) {
        return OSAL_ERR_BLOCKED;
    }

    status = osal_task_block_current_internal(OSAL_TASK_WAIT_EVENT, evt, timeout_ms);
    if (status != OSAL_OK) {
        return status;
    }

    osal_event_wait_list_append(&evt->wait_list, task);
    return OSAL_ERR_BLOCKED;
}

/* 函数说明：消费一次事件等待恢复结果，例如超时或对象被删除。 */
static osal_status_t osal_event_consume_wait_result(osal_event_t *evt, bool *handled) {
    return osal_task_consume_wait_result_internal(NULL, OSAL_TASK_WAIT_EVENT, evt, handled);
}

/* 函数说明：创建一个事件对象。 */
osal_event_t *osal_event_create(bool auto_reset) {
    osal_event_t *evt;

    if (osal_irq_is_in_isr()) {
        osal_event_report("create is not allowed in ISR context");
        return NULL;
    }

    evt = (osal_event_t *)osal_mem_alloc((uint32_t)sizeof(osal_event_t));
    if (evt == NULL) {
        return NULL;
    }

    evt->state = false;
    evt->auto_reset = auto_reset;
    evt->wait_list = NULL;
    evt->next = s_event_list;
    s_event_list = evt;
    return evt;
}

/* 函数说明：删除一个事件对象。 */
void osal_event_delete(osal_event_t *evt) {
    osal_event_t *prev = NULL;
    osal_event_t *current = s_event_list;
    uint32_t irq_state;

    if (evt == NULL) {
        return;
    }
    if (osal_irq_is_in_isr()) {
        osal_event_report("delete is not allowed in ISR context");
        return;
    }

    irq_state = osal_irq_disable();
    while (current != NULL) {
        if (current == evt) {
            if (prev == NULL) {
                s_event_list = current->next;
            } else {
                prev->next = current->next;
            }

            /*
             * 删除事件对象前，先把所有等待它的任务唤醒。
             * 恢复状态使用 OSAL_ERR_DELETED，表示“等待对象已经不存在”。
             */
            osal_event_wake_all_waiters_locked(current, OSAL_ERR_DELETED);
            osal_irq_restore(irq_state);
            osal_mem_free(current);
            return;
        }
        prev = current;
        current = current->next;
    }
    osal_irq_restore(irq_state);

    osal_event_report("delete called with inactive event handle");
}

/* 函数说明：将事件置为已触发状态，并按 auto_reset 策略唤醒等待任务。 */
osal_status_t osal_event_set(osal_event_t *evt) {
    uint32_t irq_state;

    if (!osal_event_validate_handle(evt)) {
        return OSAL_ERR_PARAM;
    }

    irq_state = osal_irq_disable();
    if (evt->auto_reset) {
        /*
         * 自动复位事件是“二值事件”，只保留一次待消费状态。
         * 如果事件已经是触发态，再次 set 不会继续累加。
         */
        if (!evt->state) {
            evt->state = true;
            /* 自动复位事件一次只允许一个等待者消费，因此只唤醒一个。 */
            osal_event_wake_one_waiter_locked(evt, OSAL_OK);
        }
    } else {
        /*
         * 手动复位事件在 set 后持续保持触发状态，
         * 因此所有等待它的任务都可以被唤醒，直到用户主动 clear。
         */
        evt->state = true;
        osal_event_wake_all_waiters_locked(evt, OSAL_OK);
    }
    osal_irq_restore(irq_state);
    return OSAL_OK;
}

/* 函数说明：清除事件触发状态。 */
osal_status_t osal_event_clear(osal_event_t *evt) {
    uint32_t irq_state;

    if (!osal_event_validate_handle(evt)) {
        return OSAL_ERR_PARAM;
    }

    irq_state = osal_irq_disable();
    evt->state = false;
    osal_irq_restore(irq_state);
    return OSAL_OK;
}

/* 函数说明：等待事件被触发或等待超时。 */
osal_status_t osal_event_wait(osal_event_t *evt, uint32_t timeout_ms) {
    uint32_t irq_state;
    osal_status_t status;
    bool handled = false;

    if (!osal_event_validate_handle(evt)) {
        return OSAL_ERR_PARAM;
    }
    if (osal_irq_is_in_isr()) {
        osal_event_report("wait is not allowed in ISR context");
        return OSAL_ERR_ISR;
    }

    /*
     * 如果当前任务是“上一次等待之后恢复回来，再次重新进入本 API”，
     * 这里会先消费恢复结果，例如超时或对象被删除。
     */
    status = osal_event_consume_wait_result(evt, &handled);
    if (handled) {
        return status;
    }

    irq_state = osal_irq_disable();
    status = osal_event_try_consume_locked(evt);
    if (status == OSAL_OK) {
        osal_irq_restore(irq_state);
        return OSAL_OK;
    }
    if (timeout_ms == 0U) {
        /* 0U 表示只检查一次当前状态，不进入等待链表。 */
        osal_irq_restore(irq_state);
        return OSAL_ERR_RESOURCE;
    }

    status = osal_event_prepare_wait_locked(evt, timeout_ms);
    osal_irq_restore(irq_state);
    return status;
}

/* 函数说明：把指定任务从某个事件对象的等待链表中移除。 */
void osal_event_cancel_wait_internal(void *event_object, osal_task_t *task) {
    osal_event_t *evt = (osal_event_t *)event_object;

    if ((evt == NULL) || (task == NULL)) {
        return;
    }

    osal_event_wait_list_remove(&evt->wait_list, task);
}

#endif /* OSAL_CFG_ENABLE_EVENT */
