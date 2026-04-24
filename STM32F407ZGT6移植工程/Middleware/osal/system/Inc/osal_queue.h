#ifndef OSAL_QUEUE_H
#define OSAL_QUEUE_H

#include <stdint.h>
#include "osal.h"

#if !OSAL_CFG_ENABLE_QUEUE
#error "OSAL queue module is disabled. Enable OSAL_CFG_ENABLE_QUEUE in osal_config.h."
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct osal_queue osal_queue_t;

/*
 * 队列句柄约定：
 * 1. 队列控制块和数据区统一来自 osal_mem。
 * 2. delete(NULL) 是安全空操作。
 * 3. delete 成功后，句柄立即失效。
 * 4. 当前队列是“固定项大小队列”，不是可变长消息队列。
 */

/**
 * @brief 创建一个定长定项大小的循环队列。
 * @param length 队列可容纳的元素个数。
 * @param item_size 每个元素的固定字节数。
 * @return 成功返回队列句柄，失败返回 NULL。
 */
osal_queue_t *osal_queue_create(uint32_t length, uint32_t item_size);

/**
 * @brief 删除一个队列句柄及其内部存储区。
 * @param q 队列句柄。
 * @note delete(NULL) 是安全空操作。
 */
void osal_queue_delete(osal_queue_t *q);

/**
 * @brief 读取当前队列中的元素数量。
 * @param q 队列句柄。
 * @return 当前队列元素数；句柄非法时返回 0。
 */
uint32_t osal_queue_get_count(const osal_queue_t *q);

/*
 * timeout_ms 语义：
 * 1. timeout_ms = 0U
 *    立即尝试一次；资源不满足就立刻返回。
 * 2. timeout_ms = N
 *    在 N ms 时间窗口内反复尝试；成功返回 OSAL_OK，超时返回 OSAL_ERR_TIMEOUT。
 *
 * 这不是任务挂起语义，也不会保存任务上下文。
 * 当 timeout_ms > 0 时，本质上是“基于系统 tick 的同步忙等超时重试”。
 */
/**
 * @brief 向队列尾部发送一个元素。
 * @param q 队列句柄。
 * @param item 待发送元素地址。
 * @param timeout_ms 同步重试窗口，单位 ms。
 * @return 成功返回 OSAL_OK；满队列且超时返回 OSAL_ERR_TIMEOUT。
 */
/*
 * 当前 queue 的模型说明：
 * 1. 它本质上是固定单元大小的 ring buffer。
 * 2. 当前实现内部没有等待链表。
 * 3. send/recv 不会把某个等待任务从 waiting 切回 ready。
 * 4. 这里的“事件驱动”只表示队列状态可能在重试期间被 ISR/DMA 等异步路径改变。
 */
osal_status_t osal_queue_send(osal_queue_t *q, const void *item, uint32_t timeout_ms);

/**
 * @brief 从队列头部接收一个元素。
 * @param q 队列句柄。
 * @param item 接收缓冲区地址。
 * @param timeout_ms 同步重试窗口，单位 ms。
 * @return 成功返回 OSAL_OK；空队列且超时返回 OSAL_ERR_TIMEOUT。
 */
osal_status_t osal_queue_recv(osal_queue_t *q, void *item, uint32_t timeout_ms);

/*
 * ISR 版本接口始终是立即尝试，不接受 timeout_ms。
 */
/**
 * @brief 在 ISR 中向队列尾部立即发送一个元素。
 * @param q 队列句柄。
 * @param item 待发送元素地址。
 * @return 成功返回 OSAL_OK；资源不足时返回 OSAL_ERR_RESOURCE。
 */
osal_status_t osal_queue_send_from_isr(osal_queue_t *q, const void *item);

/**
 * @brief 在 ISR 中从队列头部立即接收一个元素。
 * @param q 队列句柄。
 * @param item 接收缓冲区地址。
 * @return 成功返回 OSAL_OK；资源不足时返回 OSAL_ERR_RESOURCE。
 */
osal_status_t osal_queue_recv_from_isr(osal_queue_t *q, void *item);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_QUEUE_H */
