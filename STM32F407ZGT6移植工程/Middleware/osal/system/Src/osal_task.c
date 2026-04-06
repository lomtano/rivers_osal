#include "../Inc/osal_task.h"
#include "../Inc/osal_mem.h"
#include "../Inc/osal_timer.h"
#include "../Inc/osal_irq.h"

#ifndef OSAL_TASK_LOW_SCAN_PERIOD
#define OSAL_TASK_LOW_SCAN_PERIOD 4U
#endif

/*
 * 说明：
 * 1. 下面这组内部前置声明只给 system/Src 内部模块互相调用。
 * 2. 它们不属于公开 API，因此不放进外部头文件。
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

#if OSAL_CFG_ENABLE_QUEUE
void osal_queue_cancel_wait_internal(void *queue_object,
                                     osal_task_t *task,
                                     osal_task_wait_reason_t reason);
#endif

struct osal_task {
    osal_task_fn_t fn;
    void *arg;
    osal_task_state_t state;
    osal_task_priority_t priority;
    uint32_t dispatch_tick_ms;
    uint32_t periodic_wake_ms;
    uint32_t wait_start_ms;
    uint32_t wait_timeout_ms;
    uint32_t wait_deadline_ms;
    osal_task_wait_reason_t wait_reason;
    void *wait_object;
    osal_task_wait_reason_t resume_reason;
    void *resume_object;
    osal_status_t resume_status;
    bool periodic_sleep_initialized;
    bool wait_forever;
    bool resume_valid;
    struct osal_task *next;
    struct osal_task *wait_next;
};

static osal_task_t *s_task_lists[OSAL_TASK_PRIORITY_COUNT] = {0};
static osal_task_t *s_current_task = NULL;
static uint8_t s_scheduler_depth = 0U;
static uint32_t s_low_scan_count = 0U;

/* 函数说明：执行一次内部协作式调度循环。 */
static void osal_run_internal(osal_task_t *skip_task);

/* 函数说明：输出任务模块调试诊断信息。 */
static void osal_task_report(const char *message) {
    OSAL_DEBUG_REPORT("task", message);
}

/* 函数说明：检查任务优先级参数是否合法。 */
static bool osal_task_priority_is_valid(osal_task_priority_t priority) {
    return ((uint32_t)priority < (uint32_t)OSAL_TASK_PRIORITY_COUNT);
}

/* 函数说明：清空任务当前的等待状态。 */
static void osal_task_clear_wait_state(osal_task_t *task) {
    if (task == NULL) {
        return;
    }

    task->wait_start_ms = 0U;
    task->wait_timeout_ms = 0U;
    task->wait_deadline_ms = 0U;
    task->wait_reason = OSAL_TASK_WAIT_NONE;
    task->wait_object = NULL;
    task->wait_forever = false;
    task->wait_next = NULL;
}

/* 函数说明：清空任务上一次等待恢复结果。 */
static void osal_task_clear_resume_state(osal_task_t *task) {
    if (task == NULL) {
        return;
    }

    task->resume_reason = OSAL_TASK_WAIT_NONE;
    task->resume_object = NULL;
    task->resume_status = OSAL_OK;
    task->resume_valid = false;
}

/* 函数说明：把任务切换为 READY，并按需记录本次恢复结果。 */
static void osal_task_make_ready(osal_task_t *task, osal_status_t resume_status) {
    osal_task_wait_reason_t previous_reason;
    void *previous_object;

    if (task == NULL) {
        return;
    }

    previous_reason = task->wait_reason;
    previous_object = task->wait_object;
    task->state = OSAL_TASK_READY;

    if (resume_status != OSAL_OK) {
        task->resume_reason = previous_reason;
        task->resume_object = previous_object;
        task->resume_status = resume_status;
        task->resume_valid = true;
    }

    osal_task_clear_wait_state(task);
}

/* 函数说明：如果任务正等待阻塞对象，则先把它从对应等待链表中摘除。 */
static void osal_task_detach_wait_object(osal_task_t *task) {
    if (task == NULL) {
        return;
    }

#if OSAL_CFG_ENABLE_QUEUE
    if ((task->wait_reason == OSAL_TASK_WAIT_QUEUE_SEND) ||
        (task->wait_reason == OSAL_TASK_WAIT_QUEUE_RECV)) {
        if (task->wait_object != NULL) {
            osal_queue_cancel_wait_internal(task->wait_object, task, task->wait_reason);
        }
    }
#endif

    osal_task_clear_wait_state(task);
}

/* 函数说明：检查任务句柄是否仍在调度链表中。 */
static bool osal_task_contains(osal_task_t *task) {
    uint32_t priority_idx;

    if (task == NULL) {
        return false;
    }

    for (priority_idx = (uint32_t)OSAL_TASK_PRIORITY_HIGH;
         priority_idx < (uint32_t)OSAL_TASK_PRIORITY_COUNT;
         ++priority_idx) {
        osal_task_t *current = s_task_lists[priority_idx];

        while (current != NULL) {
            if (current == task) {
                return true;
            }
            current = current->next;
        }
    }

    return false;
}

/* 函数说明：将任务追加到指定优先级链表尾部。 */
static void osal_task_list_append(osal_task_t **head, osal_task_t *task) {
    osal_task_t *current;

    if ((head == NULL) || (task == NULL)) {
        return;
    }

    task->next = NULL;
    if (*head == NULL) {
        *head = task;
        return;
    }

    current = *head;
    while (current->next != NULL) {
        current = current->next;
    }
    current->next = task;
}

/* 函数说明：从指定优先级链表中移除任务。 */
static bool osal_task_list_remove(osal_task_t **head, osal_task_t *task) {
    osal_task_t *prev = NULL;
    osal_task_t *current;

    if ((head == NULL) || (*head == NULL) || (task == NULL)) {
        return false;
    }

    current = *head;
    while (current != NULL) {
        if (current == task) {
            if (prev == NULL) {
                *head = current->next;
            } else {
                prev->next = current->next;
            }
            current->next = NULL;
            return true;
        }
        prev = current;
        current = current->next;
    }

    return false;
}

/* 函数说明：检查阻塞任务是否因到期而应被唤醒。 */
static void osal_task_check_wait_timeout(osal_task_t *task, uint32_t now_ms) {
    if ((task == NULL) || (task->state != OSAL_TASK_BLOCKED)) {
        return;
    }
    if ((task->wait_reason == OSAL_TASK_WAIT_NONE) || task->wait_forever) {
        return;
    }
    if ((int32_t)(now_ms - task->wait_deadline_ms) < 0) {
        return;
    }

    switch (task->wait_reason) {
    case OSAL_TASK_WAIT_SLEEP:
        osal_task_make_ready(task, OSAL_OK);
        break;

    case OSAL_TASK_WAIT_QUEUE_SEND:
    case OSAL_TASK_WAIT_QUEUE_RECV:
#if OSAL_CFG_ENABLE_QUEUE
        if (task->wait_object != NULL) {
            osal_queue_cancel_wait_internal(task->wait_object, task, task->wait_reason);
        }
#endif
        osal_task_make_ready(task, OSAL_ERR_TIMEOUT);
        break;

    case OSAL_TASK_WAIT_NONE:
    default:
        osal_task_make_ready(task, OSAL_OK);
        break;
    }
}

/* 函数说明：按当前优先级链表执行一轮任务扫描。 */
static bool osal_run_priority_list(osal_task_t *head, osal_task_t *skip_task, uint32_t now_ms) {
    osal_task_t *outer_task = s_current_task;
    osal_task_t *task = head;
    bool ran = false;

    while (task != NULL) {
        osal_task_t *next = task->next;
        osal_task_t *t = task;

        if (t == skip_task) {
            task = next;
            continue;
        }

        osal_task_check_wait_timeout(t, now_ms);

        if (t->state == OSAL_TASK_READY) {
            ran = true;
            t->state = OSAL_TASK_RUNNING;
            t->dispatch_tick_ms = osal_timer_get_uptime_ms();
            s_current_task = t;
            t->fn(t->arg);
            if (t->state == OSAL_TASK_RUNNING) {
                t->state = OSAL_TASK_READY;
            }
            s_current_task = outer_task;
        }

        task = next;
    }

    s_current_task = outer_task;
    return ran;
}

/* 函数说明：创建一个任务控制块并挂入调度器。 */
osal_task_t *osal_task_create(osal_task_fn_t fn, void *arg, osal_task_priority_t priority) {
    osal_task_t *task;

    if (osal_irq_is_in_isr()) {
        osal_task_report("create is not allowed in ISR context");
        return NULL;
    }
    if ((fn == NULL) || !osal_task_priority_is_valid(priority)) {
        osal_task_report("create called with invalid function or priority");
        return NULL;
    }

    task = (osal_task_t *)osal_mem_alloc((uint32_t)sizeof(osal_task_t));
    if (task == NULL) {
        return NULL;
    }

    task->fn = fn;
    task->arg = arg;
    task->state = OSAL_TASK_SUSPENDED;
    task->priority = priority;
    task->dispatch_tick_ms = 0U;
    task->periodic_wake_ms = 0U;
    task->periodic_sleep_initialized = false;
    task->next = NULL;
    task->wait_next = NULL;
    osal_task_clear_wait_state(task);
    osal_task_clear_resume_state(task);
    osal_task_list_append(&s_task_lists[priority], task);
    return task;
}

/* 函数说明：删除一个任务控制块。 */
void osal_task_delete(osal_task_t *task) {
    osal_task_priority_t priority;

    if (task == NULL) {
        return;
    }
    if (osal_irq_is_in_isr()) {
        osal_task_report("delete is not allowed in ISR context");
        return;
    }
    if (!osal_task_contains(task)) {
        osal_task_report("delete called with inactive task handle");
        return;
    }

    osal_task_detach_wait_object(task);
    priority = task->priority;
    if (!osal_task_list_remove(&s_task_lists[priority], task)) {
        osal_task_report("delete failed to unlink task handle");
        return;
    }

    if (task == s_current_task) {
        s_current_task = NULL;
    }
    osal_mem_free(task);
}

/* 函数说明：将任务切换到可运行状态。 */
osal_status_t osal_task_start(osal_task_t *task) {
    if (osal_irq_is_in_isr()) {
        osal_task_report("start is not allowed in ISR context");
        return OSAL_ERR_ISR;
    }
    if (!osal_task_contains(task)) {
        osal_task_report("start called with inactive task handle");
        return OSAL_ERR_PARAM;
    }

    osal_task_detach_wait_object(task);
    osal_task_clear_resume_state(task);
    task->state = OSAL_TASK_READY;
    task->periodic_sleep_initialized = false;
    return OSAL_OK;
}

/* 函数说明：将任务切换到停止状态。 */
osal_status_t osal_task_stop(osal_task_t *task) {
    if (osal_irq_is_in_isr()) {
        osal_task_report("stop is not allowed in ISR context");
        return OSAL_ERR_ISR;
    }
    if (!osal_task_contains(task)) {
        osal_task_report("stop called with inactive task handle");
        return OSAL_ERR_PARAM;
    }

    osal_task_detach_wait_object(task);
    task->state = OSAL_TASK_SUSPENDED;
    task->periodic_sleep_initialized = false;
    return OSAL_OK;
}

/* 函数说明：让任务进入阻塞睡眠状态。 */
osal_status_t osal_task_sleep(osal_task_t *task, uint32_t ms) {
    uint32_t now_ms;

    if (osal_irq_is_in_isr()) {
        osal_task_report("sleep is not allowed in ISR context");
        return OSAL_ERR_ISR;
    }

    if (task == NULL) {
        task = s_current_task;
    }

    if (!osal_task_contains(task)) {
        osal_task_report("sleep called with inactive task handle");
        return OSAL_ERR_PARAM;
    }

    now_ms = osal_timer_get_uptime_ms();
    osal_task_clear_resume_state(task);
    task->wait_start_ms = now_ms;
    task->wait_timeout_ms = ms;
    task->wait_deadline_ms = now_ms + ms;
    task->wait_reason = OSAL_TASK_WAIT_SLEEP;
    task->wait_object = NULL;
    task->wait_forever = false;
    task->state = OSAL_TASK_BLOCKED;
    task->wait_next = NULL;
    task->periodic_sleep_initialized = false;
    return OSAL_OK;
}

/* 函数说明：让任务按照内部维护的绝对周期休眠到下一次唤醒点。 */
osal_status_t osal_task_sleep_until(osal_task_t *task, uint32_t period_ms) {
    uint32_t now_ms;
    uint32_t next_wake_ms;

    if (osal_irq_is_in_isr()) {
        osal_task_report("sleep_until is not allowed in ISR context");
        return OSAL_ERR_ISR;
    }

    if (task == NULL) {
        task = s_current_task;
    }

    if (!osal_task_contains(task)) {
        osal_task_report("sleep_until called with inactive task handle");
        return OSAL_ERR_PARAM;
    }

    now_ms = osal_timer_get_uptime_ms();
    if (!task->periodic_sleep_initialized) {
        task->periodic_wake_ms = (task->dispatch_tick_ms != 0U) ? task->dispatch_tick_ms : now_ms;
        task->periodic_sleep_initialized = true;
    }

    next_wake_ms = task->periodic_wake_ms + period_ms;
    task->periodic_wake_ms = next_wake_ms;

    if ((int32_t)(now_ms - next_wake_ms) >= 0) {
        return OSAL_OK;
    }

    osal_task_clear_resume_state(task);
    task->wait_start_ms = now_ms;
    task->wait_timeout_ms = (uint32_t)(next_wake_ms - now_ms);
    task->wait_deadline_ms = next_wake_ms;
    task->wait_reason = OSAL_TASK_WAIT_SLEEP;
    task->wait_object = NULL;
    task->wait_forever = false;
    task->state = OSAL_TASK_BLOCKED;
    task->wait_next = NULL;
    return OSAL_OK;
}

/* 函数说明：执行一次内部协作式调度循环。 */
static void osal_run_internal(osal_task_t *skip_task) {
    uint32_t now_ms = osal_timer_get_uptime_ms();
    bool ran_high;
    bool ran_medium;
    bool should_scan_low;

    ++s_scheduler_depth;

    ran_high = osal_run_priority_list(s_task_lists[OSAL_TASK_PRIORITY_HIGH], skip_task, now_ms);
    ran_medium = osal_run_priority_list(s_task_lists[OSAL_TASK_PRIORITY_MEDIUM], skip_task, now_ms);

    should_scan_low = (!ran_high && !ran_medium);
    if (!should_scan_low) {
        ++s_low_scan_count;
        if (s_low_scan_count >= OSAL_TASK_LOW_SCAN_PERIOD) {
            should_scan_low = true;
        }
    }

    if (should_scan_low) {
        s_low_scan_count = 0U;
        (void)osal_run_priority_list(s_task_lists[OSAL_TASK_PRIORITY_LOW], skip_task, now_ms);
    }

    --s_scheduler_depth;
}

/* 函数说明：主动让出一次协作式调度执行机会。 */
void osal_task_yield(void) {
    if (osal_irq_is_in_isr()) {
        osal_task_report("yield is not allowed in ISR context");
        return;
    }

    osal_timer_poll();
    if (s_current_task == NULL) {
        return;
    }
    osal_run_internal(s_current_task);
}

/* 函数说明：执行一次 OSAL 主调度入口。 */
void osal_run(void) {
    if (osal_irq_is_in_isr()) {
        osal_task_report("osal_run is not allowed in ISR context");
        return;
    }

    osal_timer_poll();
    if ((s_scheduler_depth != 0U) && (s_current_task == NULL)) {
        return;
    }
    osal_run_internal(NULL);
}

/* 函数说明：获取当前正在运行的任务句柄。 */
osal_task_t *osal_task_get_current_internal(void) {
    return s_current_task;
}

/* 函数说明：判断任务句柄是否仍然有效。 */
bool osal_task_contains_internal(osal_task_t *task) {
    return osal_task_contains(task);
}

/* 函数说明：读取任务的调度优先级。 */
osal_task_priority_t osal_task_get_priority_internal(const osal_task_t *task) {
    return (task != NULL) ? task->priority : OSAL_TASK_PRIORITY_LOW;
}

/* 函数说明：读取任务在等待链表中的下一个节点。 */
osal_task_t *osal_task_get_wait_next_internal(const osal_task_t *task) {
    return (task != NULL) ? task->wait_next : NULL;
}

/* 函数说明：设置任务在等待链表中的下一个节点。 */
void osal_task_set_wait_next_internal(osal_task_t *task, osal_task_t *next) {
    if (task != NULL) {
        task->wait_next = next;
    }
}

/* 函数说明：判断任务是否正在等待指定对象和等待原因。 */
bool osal_task_is_waiting_internal(const osal_task_t *task,
                                   osal_task_wait_reason_t reason,
                                   const void *object) {
    if (task == NULL) {
        return false;
    }

    return (task->state == OSAL_TASK_BLOCKED) &&
           (task->wait_reason == reason) &&
           (task->wait_object == object);
}

/* 函数说明：把当前任务挂起到指定等待对象上。 */
osal_status_t osal_task_block_current_internal(osal_task_wait_reason_t reason,
                                               void *object,
                                               uint32_t timeout_ms) {
    osal_task_t *task = s_current_task;
    uint32_t now_ms;

    if (task == NULL) {
        osal_task_report("blocking wait requires current task context");
        return OSAL_ERROR;
    }
    if (!osal_task_contains(task)) {
        osal_task_report("block_current called with inactive task handle");
        return OSAL_ERR_PARAM;
    }

    now_ms = osal_timer_get_uptime_ms();
    osal_task_clear_resume_state(task);
    task->wait_start_ms = now_ms;
    task->wait_timeout_ms = timeout_ms;
    task->wait_deadline_ms = now_ms + timeout_ms;
    task->wait_reason = reason;
    task->wait_object = object;
    task->wait_forever = (timeout_ms == OSAL_WAIT_FOREVER);
    task->wait_next = NULL;
    task->state = OSAL_TASK_BLOCKED;
    task->periodic_sleep_initialized = false;
    return OSAL_OK;
}

/* 函数说明：唤醒一个已阻塞任务，并可附带恢复结果。 */
void osal_task_wake_internal(osal_task_t *task, osal_status_t resume_status) {
    if ((task == NULL) || !osal_task_contains(task)) {
        return;
    }

    osal_task_make_ready(task, resume_status);
}

/* 函数说明：消费一次等待恢复结果，例如超时返回。 */
osal_status_t osal_task_consume_wait_result_internal(osal_task_t *task,
                                                     osal_task_wait_reason_t reason,
                                                     const void *object,
                                                     bool *handled) {
    if (handled != NULL) {
        *handled = false;
    }

    if (task == NULL) {
        task = s_current_task;
    }
    if ((task == NULL) || !osal_task_contains(task)) {
        return OSAL_ERR_PARAM;
    }

    if (!task->resume_valid) {
        return OSAL_OK;
    }
    if ((task->resume_reason != reason) || (task->resume_object != object)) {
        return OSAL_OK;
    }

    if (handled != NULL) {
        *handled = true;
    }

    {
        osal_status_t status = task->resume_status;
        osal_task_clear_resume_state(task);
        return status;
    }
}
