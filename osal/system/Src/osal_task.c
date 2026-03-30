#include "../Inc/osal_task.h"
#include "../Inc/osal_mem.h"
#include "../Inc/osal_timer.h"

/*
 * 低优先级任务不会每轮都扫描。
 * 这样可以在裸机轮询框架里，优先保证高/中优先级短任务的响应速度，
 * 同时也给低优先级任务保留固定的运行机会，避免长期饿死。
 */
#ifndef OSAL_TASK_LOW_SCAN_PERIOD
#define OSAL_TASK_LOW_SCAN_PERIOD 4U
#endif

struct osal_task {
    osal_task_fn_t fn;
    void *arg;
    osal_task_state_t state;
    osal_task_priority_t priority;
    uint32_t sleep_start_ms;
    uint32_t sleep_timeout_ms;
    struct osal_task *next;
};

static osal_task_t *s_task_lists[OSAL_TASK_PRIORITY_COUNT] = {0};
static osal_task_t *s_current_task = NULL;
static uint8_t s_scheduler_depth = 0U;
static uint32_t s_low_scan_count = 0U;

static void osal_run_internal(osal_task_t *skip_task);

/* 判断优先级参数是否合法。 */
static bool osal_task_priority_is_valid(osal_task_priority_t priority) {
    return ((uint32_t)priority < (uint32_t)OSAL_TASK_PRIORITY_COUNT);
}

/* 判断某个任务是否已经挂在任务链表里。 */
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

/* 将任务追加到对应优先级链表尾部，保持同级任务创建顺序。 */
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

/* 从对应优先级链表中移除任务。 */
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

/* 对某个优先级链表执行一次扫描，返回这一轮是否真的运行过任务。 */
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

        if (t->state == OSAL_TASK_BLOCKED &&
            (uint32_t)(now_ms - t->sleep_start_ms) >= t->sleep_timeout_ms) {
            t->state = OSAL_TASK_READY;
        }

        if (t->state == OSAL_TASK_READY) {
            ran = true;
            t->state = OSAL_TASK_RUNNING;
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

osal_task_t *osal_task_create(osal_task_fn_t fn, void *arg, osal_task_priority_t priority) {
    osal_task_t *task;

    if ((fn == NULL) || !osal_task_priority_is_valid(priority)) {
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
    task->sleep_start_ms = 0U;
    task->sleep_timeout_ms = 0U;
    task->next = NULL;
    osal_task_list_append(&s_task_lists[priority], task);
    return task;
}

void osal_task_delete(osal_task_t *task) {
    osal_task_priority_t priority;

    if ((task == NULL) || !osal_task_priority_is_valid(task->priority)) {
        return;
    }

    priority = task->priority;
    if (!osal_task_list_remove(&s_task_lists[priority], task)) {
        return;
    }

    if (task == s_current_task) {
        s_current_task = NULL;
    }
    osal_mem_free(task);
}

osal_status_t osal_task_start(osal_task_t *task) {
    if (!osal_task_contains(task)) {
        return OSAL_ERR_PARAM;
    }

    task->state = OSAL_TASK_READY;
    return OSAL_OK;
}

osal_status_t osal_task_stop(osal_task_t *task) {
    if (!osal_task_contains(task)) {
        return OSAL_ERR_PARAM;
    }

    task->state = OSAL_TASK_SUSPENDED;
    return OSAL_OK;
}

osal_status_t osal_task_sleep(osal_task_t *task, uint32_t ms) {
    if (task == NULL) {
        task = s_current_task;
    }

    if (!osal_task_contains(task)) {
        return OSAL_ERR_PARAM;
    }

    task->sleep_start_ms = osal_timer_get_uptime_ms();
    task->sleep_timeout_ms = ms;
    task->state = OSAL_TASK_BLOCKED;
    return OSAL_OK;
}

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

void osal_task_yield(void) {
    osal_timer_poll();
    if (s_current_task == NULL) {
        return;
    }
    osal_run_internal(s_current_task);
}

void osal_run(void) {
    osal_timer_poll();
    if (s_scheduler_depth != 0U && s_current_task == NULL) {
        return;
    }
    osal_run_internal(NULL);
}
