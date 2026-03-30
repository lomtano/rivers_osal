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
#include "osal.h"
#include "osal_timer.h" // for timeout-based helper
#include "osal_irq.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct osal_queue osal_queue_t;

/**
 * @brief 通过 OSAL 统一静态堆创建一个队列。
 * @param length 队列可容纳的消息个数。
 * @param item_size 单个消息项大小，单位字节。
 * @return 队列句柄，失败时返回 NULL。
 * @note 此接口分配的是 `osal_mem` 管理的静态内存，不依赖 libc/system heap。
 */
osal_queue_t *osal_queue_create(uint32_t length, uint32_t item_size);

/**
 * @brief 基于用户提供的静态缓冲区创建一个队列。
 * @param buffer 原始队列存储区。
 * @param length 队列可容纳的消息个数。
 * @param item_size 单个消息项大小，单位字节。
 * @return 队列句柄，失败时返回 NULL。
 * @note 该接口可以存放任意“固定长度”的消息类型，包括结构体、指针、
 *       定长数组以及它们的组合，MCU 裸机场景下推荐优先使用此接口。
 */
osal_queue_t *osal_queue_create_static(void *buffer, uint32_t length, uint32_t item_size);

/**
 * @brief Destroy a queue control block.
 * @param q Queue handle.
 */
void osal_queue_delete(osal_queue_t *q);

/**
 * @brief Query how many items are currently stored in the queue.
 * @param q Queue handle.
 * @return Current queued item count.
 */
uint32_t osal_queue_get_count(const osal_queue_t *q);

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
