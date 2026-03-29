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

#ifndef OSAL_MUTEX_H
#define OSAL_MUTEX_H

#include <stdint.h>
#include "osal_status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct osal_mutex osal_mutex_t; // opaque

/**
 * @brief Create a mutex object.
 * @return Mutex handle, or NULL on allocation failure.
 */
osal_mutex_t *osal_mutex_create(void);

/**
 * @brief Destroy a mutex object.
 * @param mutex Mutex handle.
 */
void osal_mutex_delete(osal_mutex_t *mutex);

/**
 * @brief Lock a mutex with timeout support.
 * @param mutex Mutex handle.
 * @param timeout_ms Timeout in milliseconds.
 * @return OSAL status code.
 */
osal_status_t osal_mutex_lock(osal_mutex_t *mutex, uint32_t timeout_ms);

/**
 * @brief Unlock a mutex.
 * @param mutex Mutex handle.
 * @return OSAL status code.
 */
osal_status_t osal_mutex_unlock(osal_mutex_t *mutex);

#ifdef __cplusplus
}
#endif

#endif // OSAL_MUTEX_H
