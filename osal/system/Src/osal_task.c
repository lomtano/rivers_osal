#include "../Inc/osal_task.h"
#include "../Inc/osal_mem.h"
#include "../Inc/osal_timer.h"
#include "../Inc/osal_irq.h"

#ifndef OSAL_TASK_LOW_SCAN_PERIOD
#define OSAL_TASK_LOW_SCAN_PERIOD 4U
#endif

struct osal_task {
    osal_task_fn_t fn;
    void *arg;
    osal_task_state_t state;
    osal_task_priority_t priority;
    struct osal_task *next;
};

static osal_task_t *s_task_lists[OSAL_TASK_PRIORITY_COUNT] = {0};
static osal_task_t *s_current_task = NULL;
static uint8_t s_scheduler_depth = 0U;
static uint32_t s_low_scan_count = 0U;

static void osal_scheduler_dispatch(osal_task_t *skip_task);
static void osal_scheduler_step(void);

/* 函数说明：输出任务模块调试诊断信息。 */
static void osal_task_report(const char *message) {
    OSAL_DEBUG_REPORT("task", message);
}

/* 函数说明：检查任务优先级枚举值是否落在合法范围内。 */
static bool osal_task_priority_is_valid(osal_task_priority_t priority) {
    return ((uint32_t)priority < (uint32_t)OSAL_TASK_PRIORITY_COUNT);
}

/* 函数说明：遍历所有优先级链表，判断任务句柄是否仍然处于活动状态。 */
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

/* 函数说明：把任务追加到指定优先级链表尾部。 */
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

/* 函数说明：把任务从指定优先级链表中摘除。 */
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

/*
 * 函数说明：执行某一个优先级链表中的所有 ready 任务。
 * 这里先缓存 next，再调用任务函数。
 * 原因是任务函数内部可能 stop 自己，甚至触发嵌套调度；提前取 next 才能避免遍历链表时丢链。
 */
static bool osal_scheduler_execute_priority_list(osal_task_t *head, osal_task_t *skip_task) {
    osal_task_t *outer_task = s_current_task;
    osal_task_t *task = head;
    bool ran = false;

    while (task != NULL) {
        osal_task_t *next = task->next;
        osal_task_t *t = task;

        if (t == skip_task) {
            /* yield 触发的嵌套调度里，当前任务本轮不能被立即再次执行。 */
            task = next;
            continue;
        }

        if (t->state == OSAL_TASK_READY) {
            ran = true;
            /* 进入回调前先打上 RUNNING，防止本轮调度把它重复当成 ready 任务。 */
            t->state = OSAL_TASK_RUNNING;
            s_current_task = t;
            t->fn(t->arg);
            if (t->state == OSAL_TASK_RUNNING) {
                /* 如果任务函数正常 return 且没有主动 stop，自然回到 READY。 */
                t->state = OSAL_TASK_READY;
            }
            s_current_task = outer_task;
        }

        task = next;
    }

    s_current_task = outer_task;
    return ran;
}

/*
 * 函数说明：执行一轮协作式调度。
 * 调度策略是：先跑 high，再跑 medium；只有当高/中优先级本轮都没跑到，或者达到低优先级扫描周期时，
 * 才扫描 low 队列，避免低优先级完全饿死。
 */
static void osal_scheduler_dispatch(osal_task_t *skip_task) {
    bool ran_high;
    bool ran_medium;
    bool should_scan_low;

    ++s_scheduler_depth;

    ran_high = osal_scheduler_execute_priority_list(s_task_lists[OSAL_TASK_PRIORITY_HIGH], skip_task);
    ran_medium = osal_scheduler_execute_priority_list(s_task_lists[OSAL_TASK_PRIORITY_MEDIUM], skip_task);

    should_scan_low = (!ran_high && !ran_medium);
    if (!should_scan_low) {
        /* 高/中优先级持续活跃时，按周期给 low 一个最低限度的执行机会。 */
        ++s_low_scan_count;
        if (s_low_scan_count >= OSAL_TASK_LOW_SCAN_PERIOD) {
            should_scan_low = true;
        }
    }

    if (should_scan_low) {
        s_low_scan_count = 0U;
        (void)osal_scheduler_execute_priority_list(s_task_lists[OSAL_TASK_PRIORITY_LOW], skip_task);
    }

    --s_scheduler_depth;
}

/* 函数说明：创建一个新的协作式任务控制块。 */
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
    /* 新建任务默认先挂起，是否参与调度交给调用方显式决定。 */
    task->state = OSAL_TASK_SUSPENDED;
    task->priority = priority;
    task->next = NULL;
    osal_task_list_append(&s_task_lists[priority], task);
    return task;
}

/*
 * 函数说明：删除一个已经不再参与执行的任务控制块。
 * delete 会真正释放 TCB，所以不能在调度器扫描链表期间调用，
 * 否则调度器提前缓存的 next 指针可能指向已经释放的对象。
 */
void osal_task_delete(osal_task_t *task) {
    osal_task_priority_t priority;

    if (task == NULL) {
        return;
    }
    if (osal_irq_is_in_isr()) {
        osal_task_report("delete is not allowed in ISR context");
        return;
    }
    if (s_scheduler_depth != 0U) {
        osal_task_report("delete is not allowed while scheduler is dispatching");
        return;
    }
    if (!osal_task_contains(task)) {
        osal_task_report("delete called with inactive task handle");
        return;
    }
    if (task->state == OSAL_TASK_RUNNING) {
        osal_task_report("delete is not allowed while task is running");
        return;
    }

    priority = task->priority;
    if (!osal_task_list_remove(&s_task_lists[priority], task)) {
        osal_task_report("delete failed to unlink task handle");
        return;
    }

    if (task == s_current_task) {
        /* 理论上运行中的任务前面已被拦截；这里仅做保护性清理。 */
        s_current_task = NULL;
    }
    osal_mem_free(task);
}

/* 函数说明：把任务切换到 ready 状态，允许调度器执行它。 */
osal_status_t osal_task_start(osal_task_t *task) {
    if (osal_irq_is_in_isr()) {
        osal_task_report("start is not allowed in ISR context");
        return OSAL_ERR_ISR;
    }
    if (!osal_task_contains(task)) {
        osal_task_report("start called with inactive task handle");
        return OSAL_ERR_PARAM;
    }

    task->state = OSAL_TASK_READY;
    return OSAL_OK;
}

/* 函数说明：把任务切换到 suspended 状态，阻止后续调度。 */
osal_status_t osal_task_stop(osal_task_t *task) {
    if (osal_irq_is_in_isr()) {
        osal_task_report("stop is not allowed in ISR context");
        return OSAL_ERR_ISR;
    }
    if (!osal_task_contains(task)) {
        osal_task_report("stop called with inactive task handle");
        return OSAL_ERR_PARAM;
    }

    task->state = OSAL_TASK_SUSPENDED;
    return OSAL_OK;
}

/*
 * 函数说明：在当前调用栈里触发一次嵌套调度。
 * 这里不是抢占式上下文切换，也不会保存独立任务栈；
 * 它只是让当前任务主动把执行机会让给“其他 ready 任务”一轮。
 */
void osal_task_yield(void) {
    if (osal_irq_is_in_isr()) {
        osal_task_report("yield is not allowed in ISR context");
        return;
    }

    osal_timer_poll();
    if (s_current_task == NULL) {
        return;
    }

    osal_scheduler_dispatch(s_current_task);
}

/* 函数说明：执行一轮顶层协作式调度。 */
static void osal_scheduler_step(void) {
    if (osal_irq_is_in_isr()) {
        osal_task_report("scheduler step is not allowed in ISR context");
        return;
    }

    osal_timer_poll();
    if ((s_scheduler_depth != 0U) && (s_current_task == NULL)) {
        /* 仅当处于异常嵌套状态时才跳过，避免把“无当前任务”的中间态再次展开。 */
        return;
    }

    osal_scheduler_dispatch(NULL);
}

/* 函数说明：启动 OSAL 顶层调度循环，正常情况下不会再返回调用方。 */
void osal_start_system(void) {
    if (osal_irq_is_in_isr()) {
        osal_task_report("start_system is not allowed in ISR context");
        return;
    }

    for (;;) {
        osal_scheduler_step();
        OSAL_IDLE_HOOK();
    }
}
