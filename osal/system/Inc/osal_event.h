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

#ifndef OSAL_EVENT_H
#define OSAL_EVENT_H

#include <stdbool.h>
#include <stdint.h>
#include "osal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct osal_event osal_event_t; // opaque event

/**
 * @brief Create an event object.
 * @param auto_reset True to auto-clear after a successful wait.
 * @return Event handle, or NULL when allocation fails.
 */
osal_event_t *osal_event_create(bool auto_reset);

/**
 * @brief Destroy an event object.
 * @param evt Event handle returned by osal_event_create().
 */
void osal_event_delete(osal_event_t *evt);

/**
 * @brief Set an event to the signaled state.
 * @param evt Event handle.
 * @return OSAL status code.
 */
osal_status_t osal_event_set(osal_event_t *evt);

/**
 * @brief Clear an event back to the non-signaled state.
 * @param evt Event handle.
 * @return OSAL status code.
 */
osal_status_t osal_event_clear(osal_event_t *evt);

/**
 * @brief Wait until an event is signaled or a timeout expires.
 * @param evt Event handle.
 * @param timeout_ms Timeout in milliseconds.
 * @return OSAL_OK on success, OSAL_ERR_TIMEOUT on timeout.
 */
osal_status_t osal_event_wait(osal_event_t *evt, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // OSAL_EVENT_H
