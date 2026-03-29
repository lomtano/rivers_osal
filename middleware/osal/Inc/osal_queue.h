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

#ifndef OSAL_QUEUE_H
#define OSAL_QUEUE_H

#include <stdint.h>
#include "osal_status.h"
#include "osal_timer.h" // for timeout-based helper
#include "osal_irq.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct osal_queue osal_queue_t;

/**
 * @brief Create a queue on top of a user-provided storage buffer.
 * @param buffer Raw queue storage.
 * @param length Number of items the queue can hold.
 * @param item_size Size of each item in bytes.
 * @return Queue handle, or NULL on failure.
 */
osal_queue_t *osal_queue_create(void *buffer, uint32_t length, uint32_t item_size);

/**
 * @brief Destroy a queue control block.
 * @param q Queue handle.
 */
void osal_queue_delete(osal_queue_t *q);

// Non-blocking send/receive
/**
 * @brief Push one item into a queue without waiting.
 * @param q Queue handle.
 * @param item Pointer to the source item.
 * @return OSAL status code.
 */
osal_status_t osal_queue_send(osal_queue_t *q, const void *item);

/**
 * @brief Pop one item from a queue without waiting.
 * @param q Queue handle.
 * @param item Destination buffer for one item.
 * @return OSAL status code.
 */
osal_status_t osal_queue_recv(osal_queue_t *q, void *item);

// Blocking send/receive with timeout (ms)
/**
 * @brief Push one item into a queue with timeout support.
 * @param q Queue handle.
 * @param item Pointer to the source item.
 * @param timeout_ms Timeout in milliseconds.
 * @return OSAL status code.
 */
osal_status_t osal_queue_send_timeout(osal_queue_t *q, const void *item, uint32_t timeout_ms);

/**
 * @brief Pop one item from a queue with timeout support.
 * @param q Queue handle.
 * @param item Destination buffer for one item.
 * @param timeout_ms Timeout in milliseconds.
 * @return OSAL status code.
 */
osal_status_t osal_queue_recv_timeout(osal_queue_t *q, void *item, uint32_t timeout_ms);

// ISR-safe versions
/**
 * @brief Push one item into a queue from ISR context.
 * @param q Queue handle.
 * @param item Pointer to the source item.
 * @return OSAL status code.
 */
osal_status_t osal_queue_send_from_isr(osal_queue_t *q, const void *item);

/**
 * @brief Pop one item from a queue from ISR context.
 * @param q Queue handle.
 * @param item Destination buffer for one item.
 * @return OSAL status code.
 */
osal_status_t osal_queue_recv_from_isr(osal_queue_t *q, void *item);

#ifdef __cplusplus
}
#endif

#endif // OSAL_QUEUE_H
