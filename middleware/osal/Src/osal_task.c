/*
 * osal_task.c
 * Cooperative task subsystem for OSAL.
 * - Task creation/destruction
 * - Cooperative state machine (READY/RUNNING/BLOCKED/SUSPENDED)
 * - Sleep using osal_timer_get_uptime_ms() for timeout behavior
 */

#include "../Inc/osal_task.h"
#include "../Inc/osal_mem.h"
#include "../Inc/osal_timer.h"

struct osal_task {
    osal_task_fn_t fn;
    void *arg;
    osal_task_state_t state;
    uint32_t wake_ms;
    struct osal_task *next;
};

static osal_task_t *s_task_list = NULL;
static osal_task_t *s_current_task = NULL;
static uint8_t s_scheduler_depth = 0U;

static void osal_run_internal(osal_task_t *skip_task);

/* Walk the task list to validate a handle and produce a stable index. */
static int osal_task_find_idx(osal_task_t *task) {
    int idx = 0;
    osal_task_t *current = s_task_list;

    if (!task) return -1;
    while (current != NULL) {
        if (current == task) return idx;
        current = current->next;
        ++idx;
    }
    return -1;
}

/* Allocate a task control block from the unified OSAL heap. */
osal_task_t *osal_task_create(osal_task_fn_t fn, void *arg) {
    osal_task_t *task;

    if (!fn) return NULL;
    task = (osal_task_t *)osal_mem_alloc((uint32_t)sizeof(osal_task_t));
    if (task == NULL) return NULL;

    task->fn = fn;
    task->arg = arg;
    task->state = OSAL_TASK_SUSPENDED;
    task->wake_ms = 0U;
    task->next = s_task_list;
    s_task_list = task;
    return task;
}

/* Remove a task from the scheduler and free its control block. */
void osal_task_delete(osal_task_t *task) {
    osal_task_t *prev = NULL;
    osal_task_t *current = s_task_list;

    while (current != NULL) {
        if (current == task) {
            if (prev == NULL) {
                s_task_list = current->next;
            } else {
                prev->next = current->next;
            }
            if (current == s_current_task) {
                s_current_task = NULL;
            }
            osal_mem_free(current);
            return;
        }
        prev = current;
        current = current->next;
    }
}

/* Transition a task into ready state so the scheduler can run it. */
osal_status_t osal_task_start(osal_task_t *task) {
    if (osal_task_find_idx(task) < 0) return OSAL_ERR_PARAM;
    task->state = OSAL_TASK_READY;
    return OSAL_OK;
}

/* Transition a task into suspended state. */
osal_status_t osal_task_stop(osal_task_t *task) {
    if (osal_task_find_idx(task) < 0) return OSAL_ERR_PARAM;
    task->state = OSAL_TASK_SUSPENDED;
    return OSAL_OK;
}

/* Block one task until its wake-up tick is reached. */
osal_status_t osal_task_sleep(osal_task_t *task, uint32_t ms) {
    if (task == NULL) {
        task = s_current_task;
    }
    if (osal_task_find_idx(task) < 0) return OSAL_ERR_PARAM;
    task->wake_ms = osal_timer_get_uptime_ms() + ms;
    task->state = OSAL_TASK_BLOCKED;
    return OSAL_OK;
}

/* Run one scheduler pass while optionally skipping the current yielding task. */
static void osal_run_internal(osal_task_t *skip_task) {
    uint32_t now = osal_timer_get_uptime_ms();
    osal_task_t *outer_task = s_current_task;
    osal_task_t *task = s_task_list;
    ++s_scheduler_depth;
    while (task != NULL) {
        osal_task_t *next = task->next;
        osal_task_t *t = task;
        if (t == skip_task) {
            task = next;
            continue;
        }

        if (t->state == OSAL_TASK_BLOCKED && (int32_t)(now - t->wake_ms) >= 0) {
            t->state = OSAL_TASK_READY;
        }

        if (t->state == OSAL_TASK_READY) {
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
    --s_scheduler_depth;
    s_current_task = outer_task;
}

/* Cooperatively hand execution to other ready tasks and software timers. */
void osal_task_yield(void) {
    osal_timer_poll();
    if (s_current_task == NULL) {
        return;
    }
    osal_run_internal(s_current_task);
}

/* Public cooperative scheduler entry point. */
void osal_run(void) {
    osal_timer_poll();
    if (s_scheduler_depth != 0U && s_current_task == NULL) {
        return;
    }
    osal_run_internal(NULL);
}
