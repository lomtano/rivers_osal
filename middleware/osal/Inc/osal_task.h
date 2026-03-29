/******************************************************************************
 * Copyright (C) 2024-2026 rivers. All rights reserved.
 *
 * @author JH
 *
 * @version V1.0 2023-12-03
 *
 * @note 1 tab == 4 spaces!
 *
 *****************************************************************************/

#ifndef OSAL_TASK_H
#define OSAL_TASK_H

#include <stdbool.h>
#include "osal_status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*osal_task_fn_t)(void *arg);

typedef struct osal_task osal_task_t; // opaque

typedef enum {
    OSAL_TASK_READY = 0,
    OSAL_TASK_RUNNING,
    OSAL_TASK_BLOCKED,
    OSAL_TASK_SUSPENDED
} osal_task_state_t;

/**
 * @brief Create a cooperative task object.
 * @param fn Task entry function.
 * @param arg User argument passed to fn.
 * @return Task handle, or NULL on failure.
 */
osal_task_t *osal_task_create(osal_task_fn_t fn, void *arg);

/**
 * @brief Destroy a task object.
 * @param task Task handle.
 */
void osal_task_delete(osal_task_t *task);

/**
 * @brief Mark a task ready to run.
 * @param task Task handle.
 * @return OSAL status code.
 */
osal_status_t osal_task_start(osal_task_t *task);

/**
 * @brief Stop a task from being scheduled.
 * @param task Task handle.
 * @return OSAL status code.
 */
osal_status_t osal_task_stop(osal_task_t *task);

/**
 * @brief Put a task into blocked state for a duration.
 * @param task Task handle, or NULL for the current task.
 * @param ms Sleep duration in milliseconds.
 * @return OSAL status code.
 */
osal_status_t osal_task_sleep(osal_task_t *task, uint32_t ms);

/**
 * @brief Yield cooperatively so other tasks and timers can advance.
 */
void osal_task_yield(void);

/**
 * @brief Run one scheduling pass over all ready tasks.
 */
void osal_run(void);

#ifdef __cplusplus
}
#endif

#endif // OSAL_TASK_H
