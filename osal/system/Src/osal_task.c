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

#if OSAL_CFG_ENABLE_EVENT
void osal_event_cancel_wait_internal(void *event_object, osal_task_t *task);
#endif

#if OSAL_CFG_ENABLE_MUTEX
void osal_mutex_cancel_wait_internal(void *mutex_object, osal_task_t *task);
#endif

/*
 * 任务控制块说明：
 * 1. fn/arg 是任务入口和参数。
 * 2. state/priority 决定当前是否可被调度以及属于哪条优先级链表。
 * 3. dispatch_tick_ms 记录最近一次真正进入任务函数的时间，可作为周期任务的初始锚点。
 * 4. periodic_wake_ms / periodic_sleep_initialized 用于 sleep_until 维持固定周期。
 * 5. wait_* 描述“当前因为什么原因阻塞、从什么时候开始阻塞、何时超时”。
 * 6. resume_* 描述“这次从阻塞态恢复时，恢复原因和恢复状态是什么”。
 * 7. next 是普通调度链表指针，wait_next 是挂入某个等待链表时使用的指针。
 */
struct osal_task {
    osal_task_fn_t fn;                    /* 任务入口函数。 */
    void *arg;                            /* 传给任务入口函数的用户参数。 */
    osal_task_state_t state;              /* 当前调度状态：READY/RUNNING/BLOCKED/SUSPENDED。 */
    osal_task_priority_t priority;        /* 调度检查优先级，不是抢占优先级。 */
    uint32_t dispatch_tick_ms;            /* 最近一次真正进入任务函数的系统毫秒时间。 */
    uint32_t periodic_wake_ms;            /* sleep_until 维护的“下一次周期基准时间”。 */
    uint32_t wait_start_ms;               /* 本次等待从什么时候开始。 */
    uint32_t wait_timeout_ms;             /* 本次等待最长允许持续多久。 */
    uint32_t wait_deadline_ms;            /* 本次等待理论上应在何时到期。 */
    osal_task_wait_reason_t wait_reason;  /* 当前是因为什么原因被挂起。 */
    void *wait_object;                    /* 当前等待的是哪个对象，例如某个队列。 */
    osal_task_wait_reason_t resume_reason;/* 最近一次恢复时，对应的等待原因。 */
    void *resume_object;                  /* 最近一次恢复时，对应的等待对象。 */
    osal_status_t resume_status;          /* 最近一次恢复带回来的状态码，例如超时。 */
    bool periodic_sleep_initialized;      /* 是否已经为 sleep_until 建立过周期锚点。 */
    bool wait_forever;                    /* 当前等待是否属于永久等待。 */
    bool resume_valid;                    /* resume_* 里的恢复结果当前是否有效。 */
    struct osal_task *next;               /* 挂在普通优先级链表中的下一节点。 */
    struct osal_task *wait_next;          /* 挂在等待链表中的下一节点。 */
};

/* 三条任务链表分别对应高/中/低优先级。 */
static osal_task_t *s_task_lists[OSAL_TASK_PRIORITY_COUNT] = {0};
/* 当前正在执行的任务。某些 API 允许 task==NULL 时回退到它。 */
static osal_task_t *s_current_task = NULL;
/* 记录调度器嵌套深度，避免 yield / run 互相递归时丢失上下文。 */
static uint8_t s_scheduler_depth = 0U;
/* 低优先级任务的补偿扫描计数器，避免它们被高/中优先级长期饿住。 */
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

    /* 下面这些字段一起描述“这次等待”的完整上下文，因此要成组清空。 */
    task->wait_start_ms = 0U;
    /* 0 表示当前没有有效等待时长。 */
    task->wait_timeout_ms = 0U;
    /* 清掉绝对到期时间，避免旧值被下一次等待误用。 */
    task->wait_deadline_ms = 0U;
    /* 说明当前没有任何等待原因。 */
    task->wait_reason = OSAL_TASK_WAIT_NONE;
    /* 等待对象清空，例如不再指向某个队列实例。 */
    task->wait_object = NULL;
    /* 默认不是永久等待。 */
    task->wait_forever = false;
    /* 任务已经不挂在任何等待链表上。 */
    task->wait_next = NULL;
}

/* 函数说明：清空任务上一次等待恢复结果。 */
static void osal_task_clear_resume_state(osal_task_t *task) {
    if (task == NULL) {
        return;
    }

    /* 下面这些字段只描述“上一次从阻塞态恢复”的结果，因此同样要成组清空。 */
    task->resume_reason = OSAL_TASK_WAIT_NONE;
    task->resume_object = NULL;
    task->resume_status = OSAL_OK;
    /* false 表示当前没有一份尚未被消费的恢复结果。 */
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
    /* 先把状态切回 READY，让调度器后续有机会再次检查到它。 */
    task->state = OSAL_TASK_READY;

    /*
     * 只有“非 OK 恢复”才记录 resume_*：
     * - 正常被事件唤醒时，调用者通常直接继续原业务流程即可；
     * - 超时、对象失效等异常恢复需要在下一次进入 API 时被消费掉。
     */
    if (resume_status != OSAL_OK) {
        /* 记住“它原来在等什么”，方便下次重新进入 API 时把异常结果消费掉。 */
        task->resume_reason = previous_reason;
        /* 记住“它原来在等哪个对象”，避免误把别的等待结果拿来消费。 */
        task->resume_object = previous_object;
        /* 记录本次恢复带回来的状态，例如 OSAL_ERR_TIMEOUT。 */
        task->resume_status = resume_status;
        /* 标记 resume_* 内容当前有效。 */
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
            /*
             * 如果任务当下正挂在某个队列的等待链表里，
             * 删除/停止/重启任务前要先把它从那条链表摘掉。
             * 否则队列后面再唤醒等待者时，会碰到已经无效的任务节点。
             */
            osal_queue_cancel_wait_internal(task->wait_object, task, task->wait_reason);
        }
    }
#endif

#if OSAL_CFG_ENABLE_EVENT
    if (task->wait_reason == OSAL_TASK_WAIT_EVENT) {
        if (task->wait_object != NULL) {
            /*
             * 事件等待现在也采用“挂等待链表 + 事件唤醒”的模型。
             * 所以 stop/delete 任务前，同样要先把它从事件的等待链表中摘掉。
             */
            osal_event_cancel_wait_internal(task->wait_object, task);
        }
    }
#endif

#if OSAL_CFG_ENABLE_MUTEX
    if (task->wait_reason == OSAL_TASK_WAIT_MUTEX_LOCK) {
        if (task->wait_object != NULL) {
            /*
             * 互斥量等待链表里的节点一旦失效，后续 unlock 唤醒时就会出问题。
             * 因此 stop/delete 任务时也必须先把它从互斥量等待链表中移除。
             */
            osal_mutex_cancel_wait_internal(task->wait_object, task);
        }
    }
#endif

    /* 等待对象解除后，顺手把本地等待状态也完全清空。 */
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
        /* 每个优先级各有一条普通调度链表，这里逐条遍历查找。 */
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
        /* 空链表时，当前任务直接成为头节点。 */
        *head = task;
        return;
    }

    current = *head;
    while (current->next != NULL) {
        /* 走到链表尾部，保持创建顺序。 */
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
                /* 删除的是头节点时，头指针直接后移。 */
                *head = current->next;
            } else {
                /* 删除的是中间节点时，让前驱节点跨过当前节点。 */
                prev->next = current->next;
            }
            /* 被摘下来的节点必须断开 next，避免残留旧链表关系。 */
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
        /* 没有等待原因或是永久等待时，都不做超时检查。 */
        return;
    }
    /*
     * 用差值判断而不是直接比较 now_ms >= deadline：
     * 这样在 32 位毫秒 tick 回绕后，短时间窗口内仍然能正确判断“是否到期”。
     */
    if ((int32_t)(now_ms - task->wait_deadline_ms) < 0) {
        return;
    }

    switch (task->wait_reason) {
    case OSAL_TASK_WAIT_SLEEP:
        /* sleep/sleep_until 到期后只需恢复 READY，不需要额外错误码。 */
        osal_task_make_ready(task, OSAL_OK);
        break;

    case OSAL_TASK_WAIT_QUEUE_SEND:
    case OSAL_TASK_WAIT_QUEUE_RECV:
#if OSAL_CFG_ENABLE_QUEUE
        if (task->wait_object != NULL) {
            /* 超时时先从队列等待链表中摘除，避免后续又被队列重复唤醒。 */
            osal_queue_cancel_wait_internal(task->wait_object, task, task->wait_reason);
        }
#endif
        /* 队列等待超时要带着 OSAL_ERR_TIMEOUT 返回给调用者。 */
        osal_task_make_ready(task, OSAL_ERR_TIMEOUT);
        break;

    case OSAL_TASK_WAIT_EVENT:
#if OSAL_CFG_ENABLE_EVENT
        if (task->wait_object != NULL) {
            /* 事件等待超时时，也要先从事件等待链表摘除。 */
            osal_event_cancel_wait_internal(task->wait_object, task);
        }
#endif
        osal_task_make_ready(task, OSAL_ERR_TIMEOUT);
        break;

    case OSAL_TASK_WAIT_MUTEX_LOCK:
#if OSAL_CFG_ENABLE_MUTEX
        if (task->wait_object != NULL) {
            /* 互斥量等待超时时，先把任务从等待获取锁的链表里摘掉。 */
            osal_mutex_cancel_wait_internal(task->wait_object, task);
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
        /*
         * 先缓存 next，再执行当前任务。
         * 原因是任务函数内部可能 start/stop/delete 其他任务，甚至影响当前链表结构。
         */
        osal_task_t *next = task->next;
        osal_task_t *t = task;

        if (t == skip_task) {
            task = next;
            continue;
        }

        osal_task_check_wait_timeout(t, now_ms);

        if (t->state == OSAL_TASK_READY) {
            ran = true;
            /* 标记成 RUNNING，表示这个任务当前正占用调度执行机会。 */
            t->state = OSAL_TASK_RUNNING;
            /* 记录本次真正进入任务函数的时间，供 sleep_until 首次建立周期锚点使用。 */
            t->dispatch_tick_ms = osal_timer_get_uptime_ms();
            s_current_task = t;
            t->fn(t->arg);
            if (t->state == OSAL_TASK_RUNNING) {
                /*
                 * 如果任务函数返回后状态仍然是 RUNNING，
                 * 说明它既没有自己 sleep，也没有 stop/delete 自己，
                 * 那么这里要把它恢复回 READY，供下一轮继续执行。
                 */
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
    /* 新建任务默认先挂起，只有显式 start 后才参与调度。 */
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
    /* start 的语义是“允许再次被调度”，因此直接进入 READY。 */
    task->state = OSAL_TASK_READY;
    /* 重新 start 后，周期任务应重新建立自己的周期锚点。 */
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
    /* stop 后任务不再参与调度扫描。 */
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
    /* 记录这次睡眠是从什么时候开始的。 */
    task->wait_start_ms = now_ms;
    /* 原样记下用户要求的睡眠时长。 */
    task->wait_timeout_ms = ms;
    /* 直接算出本次睡眠到期的绝对时间点。 */
    task->wait_deadline_ms = now_ms + ms;
    /* 标记“这是一种普通 sleep 等待”。 */
    task->wait_reason = OSAL_TASK_WAIT_SLEEP;
    /* sleep 不依赖任何外部同步对象，所以这里清空等待对象。 */
    task->wait_object = NULL;
    /* 普通 sleep 不是永久等待。 */
    task->wait_forever = false;
    /* 真正把任务切到 BLOCKED，调度器本轮后续不会再执行这个任务函数。 */
    task->state = OSAL_TASK_BLOCKED;
    /* sleep 不使用等待链表，所以 wait_next 必须清空。 */
    task->wait_next = NULL;
    /* 普通 sleep 会打断之前的周期节拍，所以下次 sleep_until 需要重新建锚点。 */
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
        /*
         * 第一次进入周期睡眠时，优先以“本轮任务真正被调度的时间”作为周期锚点。
         * 这样即便任务函数里先做了一点工作，再调用 sleep_until，也不会把这段执行耗时
         * 永久累计进周期。
         */
        task->periodic_wake_ms = (task->dispatch_tick_ms != 0U) ? task->dispatch_tick_ms : now_ms;
        /* 只在第一次建立锚点；后续周期都沿着这个锚点向前滚动。 */
        task->periodic_sleep_initialized = true;
    }

    next_wake_ms = task->periodic_wake_ms + period_ms;
    /* 先把内部周期基准推进到“下一次”周期点。 */
    task->periodic_wake_ms = next_wake_ms;

    /*
     * 如果当前时间已经超过下一周期点，说明任务落后于预定节拍。
     * 这里直接返回 OK，让调用者立刻继续执行一次业务逻辑，用于“追赶周期”。
     */
    if ((int32_t)(now_ms - next_wake_ms) >= 0) {
        /*
         * 如果已经晚于目标周期点，直接返回而不是再 sleep 0ms。
         * 这样业务代码能尽快补跑一次，避免周期继续越拖越慢。
         */
        return OSAL_OK;
    }

    osal_task_clear_resume_state(task);
    /* 记录现在开始等待这个周期点。 */
    task->wait_start_ms = now_ms;
    /* 还剩多少毫秒才到下一个周期点。 */
    task->wait_timeout_ms = (uint32_t)(next_wake_ms - now_ms);
    /* 绝对到期点就是 next_wake_ms。 */
    task->wait_deadline_ms = next_wake_ms;
    /* 仍然归类成 sleep 型等待，只是目标时间来自周期基准。 */
    task->wait_reason = OSAL_TASK_WAIT_SLEEP;
    task->wait_object = NULL;
    task->wait_forever = false;
    /* 切到 BLOCKED，让调度器先去运行其他任务。 */
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
    /* 记录递归深度，避免 yield -> run -> yield 时丢失调度上下文。 */

    /* 高优先级和中优先级任务每轮都扫描。 */
    ran_high = osal_run_priority_list(s_task_lists[OSAL_TASK_PRIORITY_HIGH], skip_task, now_ms);
    ran_medium = osal_run_priority_list(s_task_lists[OSAL_TASK_PRIORITY_MEDIUM], skip_task, now_ms);

    /*
     * 低优先级任务不是每轮都扫：
     * 1. 如果高/中这一轮都没跑任务，就立刻给低优先级一个机会；
     * 2. 否则累计若干轮后兜底扫描一次，避免低优先级永久饥饿。
     */
    should_scan_low = (!ran_high && !ran_medium);
    if (!should_scan_low) {
        /* 高/中优先级持续活跃时，用计数器给低优先级做保底。 */
        ++s_low_scan_count;
        if (s_low_scan_count >= OSAL_TASK_LOW_SCAN_PERIOD) {
            should_scan_low = true;
        }
    }

    if (should_scan_low) {
        /* 一旦真正扫描了低优先级，就把保底计数清零重新开始。 */
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
        /* 没有当前任务时，说明这里不是从任务函数内部调用的。 */
        return;
    }
    /* yield 只让出一次机会，不应立刻再次执行当前任务，所以把它作为 skip_task。 */
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
        /*
         * 说明当前处于某次嵌套调度收尾阶段，且已经没有可恢复的当前任务，
         * 这里直接返回，避免重复进入一轮新的调度。
         */
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
    /* 当前等待是从 now_ms 开始算的。 */
    task->wait_start_ms = now_ms;
    /* 原样保存用户传入的等待参数，后面调试和诊断都会用到。 */
    task->wait_timeout_ms = timeout_ms;
    /* 提前算出绝对超时点，后面调度器就只需要做一次差值比较。 */
    task->wait_deadline_ms = now_ms + timeout_ms;
    /* 记住“是因为什么原因被挂起的”，例如等队列可读还是等队列可写。 */
    task->wait_reason = reason;
    /* 记住“是在等哪个对象”，例如具体是哪一个队列实例。 */
    task->wait_object = object;
    /* 单独记录是否永久等待，避免后面还去做超时比较。 */
    task->wait_forever = (timeout_ms == OSAL_WAIT_FOREVER);
    /* 任务刚挂入等待对象时，等待链表指针从空开始。 */
    task->wait_next = NULL;
    /* 进入 BLOCKED 后，普通调度扫描不会再执行它，只会检查是否该恢复。 */
    task->state = OSAL_TASK_BLOCKED;
    /* 等待队列事件与周期延时无关，因此把周期锚点状态清掉。 */
    task->periodic_sleep_initialized = false;
    return OSAL_OK;
}

/* 函数说明：唤醒一个已阻塞任务，并可附带恢复结果。 */
void osal_task_wake_internal(osal_task_t *task, osal_status_t resume_status) {
    if ((task == NULL) || !osal_task_contains(task)) {
        return;
    }

    /* 统一走 make_ready，把“恢复为 READY”和“记录恢复结果”这两步一次做完。 */
    osal_task_make_ready(task, resume_status);
}

/* 函数说明：消费一次等待恢复结果，例如超时返回。 */
osal_status_t osal_task_consume_wait_result_internal(osal_task_t *task,
                                                     osal_task_wait_reason_t reason,
                                                     const void *object,
                                                     bool *handled) {
    if (handled != NULL) {
        /* 默认先告诉调用者：这次还没有消费到一份匹配的恢复结果。 */
        *handled = false;
    }

    if (task == NULL) {
        task = s_current_task;
    }
    if ((task == NULL) || !osal_task_contains(task)) {
        return OSAL_ERR_PARAM;
    }

    if (!task->resume_valid) {
        /* 没有恢复结果可消费时，说明这次不是从阻塞态恢复回来的。 */
        return OSAL_OK;
    }
    if ((task->resume_reason != reason) || (task->resume_object != object)) {
        /* 恢复结果存在，但不属于当前这类等待对象，不能误消费。 */
        return OSAL_OK;
    }

    if (handled != NULL) {
        *handled = true;
    }

    {
        osal_status_t status = task->resume_status;
        /* 恢复结果是一次性的，读完就清空。 */
        osal_task_clear_resume_state(task);
        return status;
    }
}




