#include "../Inc/osal.h"

#if OSAL_CFG_ENABLE_MUTEX

#include "../Inc/osal_mutex.h"
#include "../Inc/osal_irq.h"
#include "../Inc/osal_mem.h"
#include "../Inc/osal_task.h"

/*
 * 说明：
 * 1. mutex.c 和 queue/event 一样，复用 task.c 的内部阻塞/恢复机制。
 * 2. 这些内部接口不属于公开 API，因此仍放在源文件内部声明。
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
 * 这是最小互斥量实现：
 * 1. locked 只表示“是否已被占用”。
 * 2. wait_list 挂着所有正在等待它解锁的任务。
 * 3. next 仅用于活动对象链表管理。
 * 4. 当前实现仍然不记录 owner，也没有优先级继承。
 */
struct osal_mutex {
    volatile bool locked;
    osal_task_t *wait_list;
    struct osal_mutex *next;
};

static osal_mutex_t *s_mutex_list = NULL;

/* 函数说明：输出互斥量模块调试诊断信息。 */
static void osal_mutex_report(const char *message) {
    OSAL_DEBUG_REPORT("mutex", message);
}

/* 函数说明：检查互斥量句柄是否仍在活动链表中。 */
#if OSAL_CFG_ENABLE_DEBUG
static bool osal_mutex_contains(osal_mutex_t *mutex) {
    osal_mutex_t *current = s_mutex_list;

    while (current != NULL) {
        if (current == mutex) {
            return true;
        }
        current = current->next;
    }

    return false;
}
#endif

/* 函数说明：校验互斥量句柄是否有效。 */
static bool osal_mutex_validate_handle(const osal_mutex_t *mutex) {
    if (mutex == NULL) {
        return false;
    }
#if OSAL_CFG_ENABLE_DEBUG
    if (!osal_mutex_contains((osal_mutex_t *)mutex)) {
        osal_mutex_report("API called with inactive mutex handle");
        return false;
    }
#endif
    return true;
}

/* 函数说明：判断任务是否已经挂在互斥量等待链表中。 */
static bool osal_mutex_wait_list_contains(osal_task_t *head, osal_task_t *task) {
    while (head != NULL) {
        if (head == task) {
            return true;
        }
        head = osal_task_get_wait_next_internal(head);
    }

    return false;
}

/* 函数说明：把任务追加到互斥量等待链表尾部。 */
static void osal_mutex_wait_list_append(osal_task_t **head, osal_task_t *task) {
    osal_task_t *current;

    if ((head == NULL) || (task == NULL)) {
        return;
    }
    if (osal_mutex_wait_list_contains(*head, task)) {
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

/* 函数说明：把指定任务从互斥量等待链表中移除。 */
static void osal_mutex_wait_list_remove(osal_task_t **head, osal_task_t *task) {
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

/* 函数说明：从互斥量等待链表里弹出“最高优先级、同级 FIFO”的任务。 */
static osal_task_t *osal_mutex_wait_list_pop_best(osal_task_t **head) {
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

/* 函数说明：唤醒一个等待当前互斥量的任务。 */
static void osal_mutex_wake_one_waiter_locked(osal_mutex_t *mutex, osal_status_t resume_status) {
    osal_task_t *task;

    if (mutex == NULL) {
        return;
    }

    task = osal_mutex_wait_list_pop_best(&mutex->wait_list);
    if (task != NULL) {
        osal_task_wake_internal(task, resume_status);
    }
}

/* 函数说明：批量唤醒当前互斥量上的所有等待任务。 */
static void osal_mutex_wake_all_waiters_locked(osal_mutex_t *mutex, osal_status_t resume_status) {
    if (mutex == NULL) {
        return;
    }

    while (mutex->wait_list != NULL) {
        osal_task_t *task = osal_mutex_wait_list_pop_best(&mutex->wait_list);

        if (task == NULL) {
            break;
        }
        osal_task_wake_internal(task, resume_status);
    }
}

/* 函数说明：在临界区内尝试立刻获取互斥量。 */
static osal_status_t osal_mutex_try_lock_locked(osal_mutex_t *mutex) {
    if ((mutex == NULL) || mutex->locked) {
        return OSAL_ERR_RESOURCE;
    }

    /*
     * 看到 unlocked 后，必须在同一临界区里立刻改成 locked=true，
     * 否则别的任务或 ISR 可能在我们真正返回前抢先拿到同一个互斥量。
     */
    mutex->locked = true;
    return OSAL_OK;
}

/*
 * 说明：
 * 1. 返回 OSAL_ERR_BLOCKED 表示“当前任务已经进入 BLOCKED，并挂入等待链表”。
 * 2. 后续真正的恢复结果，会在任务再次进入 osal_mutex_lock() 时统一消费。
 */
/* 函数说明：把当前任务挂到互斥量等待链表中。 */
static osal_status_t osal_mutex_prepare_wait_locked(osal_mutex_t *mutex, uint32_t timeout_ms) {
    osal_task_t *task = osal_task_get_current_internal();
    osal_status_t status;

    if ((mutex == NULL) || (task == NULL)) {
        osal_mutex_report("blocking mutex wait requires current task context");
        return OSAL_ERROR;
    }
    if (!osal_task_contains_internal(task)) {
        osal_mutex_report("blocking mutex wait called with inactive task");
        return OSAL_ERR_PARAM;
    }
    if (osal_task_is_waiting_internal(task, OSAL_TASK_WAIT_MUTEX_LOCK, mutex)) {
        return OSAL_ERR_BLOCKED;
    }

    status = osal_task_block_current_internal(OSAL_TASK_WAIT_MUTEX_LOCK, mutex, timeout_ms);
    if (status != OSAL_OK) {
        return status;
    }

    osal_mutex_wait_list_append(&mutex->wait_list, task);
    return OSAL_ERR_BLOCKED;
}

/* 函数说明：消费一次互斥量等待恢复结果，例如超时或对象被删除。 */
static osal_status_t osal_mutex_consume_wait_result(osal_mutex_t *mutex, bool *handled) {
    return osal_task_consume_wait_result_internal(NULL, OSAL_TASK_WAIT_MUTEX_LOCK, mutex, handled);
}

/* 函数说明：创建一个互斥量对象。 */
osal_mutex_t *osal_mutex_create(void) {
    osal_mutex_t *mutex;

    if (osal_irq_is_in_isr()) {
        osal_mutex_report("create is not allowed in ISR context");
        return NULL;
    }

    mutex = (osal_mutex_t *)osal_mem_alloc((uint32_t)sizeof(osal_mutex_t));
    if (mutex == NULL) {
        return NULL;
    }

    mutex->locked = false;
    mutex->wait_list = NULL;
    mutex->next = s_mutex_list;
    s_mutex_list = mutex;
    return mutex;
}

/* 函数说明：删除一个互斥量对象。 */
void osal_mutex_delete(osal_mutex_t *mutex) {
    osal_mutex_t *prev = NULL;
    osal_mutex_t *current = s_mutex_list;
    uint32_t irq_state;

    if (mutex == NULL) {
        return;
    }
    if (osal_irq_is_in_isr()) {
        osal_mutex_report("delete is not allowed in ISR context");
        return;
    }

    irq_state = osal_irq_disable();
    while (current != NULL) {
        if (current == mutex) {
            if (prev == NULL) {
                s_mutex_list = current->next;
            } else {
                prev->next = current->next;
            }

            /* 删除对象前先把所有等待它的任务唤醒，并明确告知对象已经被删除。 */
            osal_mutex_wake_all_waiters_locked(current, OSAL_ERR_DELETED);
            osal_irq_restore(irq_state);
            osal_mem_free(current);
            return;
        }
        prev = current;
        current = current->next;
    }
    osal_irq_restore(irq_state);

    osal_mutex_report("delete called with inactive mutex handle");
}

/* 函数说明：尝试获取互斥量并按需进入事件驱动等待。 */
osal_status_t osal_mutex_lock(osal_mutex_t *mutex, uint32_t timeout_ms) {
    uint32_t irq_state;
    osal_status_t status;
    bool handled = false;

    if (!osal_mutex_validate_handle(mutex)) {
        return OSAL_ERR_PARAM;
    }
    if (osal_irq_is_in_isr()) {
        osal_mutex_report("lock is not allowed in ISR context");
        return OSAL_ERR_ISR;
    }

    status = osal_mutex_consume_wait_result(mutex, &handled);
    if (handled) {
        return status;
    }

    irq_state = osal_irq_disable();
    status = osal_mutex_try_lock_locked(mutex);
    if (status == OSAL_OK) {
        osal_irq_restore(irq_state);
        return OSAL_OK;
    }
    if (timeout_ms == 0U) {
        osal_irq_restore(irq_state);
        return OSAL_ERR_RESOURCE;
    }

    status = osal_mutex_prepare_wait_locked(mutex, timeout_ms);
    osal_irq_restore(irq_state);
    return status;
}

/* 函数说明：释放已经获取到的互斥量。 */
osal_status_t osal_mutex_unlock(osal_mutex_t *mutex) {
    uint32_t irq_state;

    if (!osal_mutex_validate_handle(mutex)) {
        return OSAL_ERR_PARAM;
    }
    if (osal_irq_is_in_isr()) {
        osal_mutex_report("unlock is not allowed in ISR context");
        return OSAL_ERR_ISR;
    }

    irq_state = osal_irq_disable();
    /*
     * 当前最小实现没有 owner 检查，因此任何持有句柄的任务都能解锁。
     * unlock 后先把锁状态清成 false，再唤醒一个等待者去重新竞争这把锁。
     */
    mutex->locked = false;
    osal_mutex_wake_one_waiter_locked(mutex, OSAL_OK);
    osal_irq_restore(irq_state);
    return OSAL_OK;
}

/* 函数说明：把指定任务从某个互斥量对象的等待链表中移除。 */
void osal_mutex_cancel_wait_internal(void *mutex_object, osal_task_t *task) {
    osal_mutex_t *mutex = (osal_mutex_t *)mutex_object;

    if ((mutex == NULL) || (task == NULL)) {
        return;
    }

    osal_mutex_wait_list_remove(&mutex->wait_list, task);
}

#endif /* OSAL_CFG_ENABLE_MUTEX */
