#ifndef OSAL_QUEUE_H
#define OSAL_QUEUE_H

#include <stdint.h>
#include "osal.h"

#if !OSAL_CFG_ENABLE_QUEUE
#error "OSAL queue module is disabled. Enable OSAL_CFG_ENABLE_QUEUE in osal.h."
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct osal_queue osal_queue_t;

/*
 * 队列句柄契约：
 * 1. create / create_static 成功后，队列控制块句柄所有权归调用方。
 * 2. delete(NULL) 是安全空操作。
 * 3. delete() 成功后，句柄立即失效，不能再次 send / recv / get_count / delete。
 * 4. create() 模式下，消息缓冲区由 OSAL 堆管理；create_static() 模式下，消息缓冲区仍归用户所有。
 * 5. debug 打开时，可检测到的非法句柄、重复 delete 会通过 OSAL_DEBUG_HOOK 报告。
 *
 * 接口能力矩阵：
 * - create / create_static / delete: 任务态
 * - get_count / send / recv / send_from_isr / recv_from_isr: 任务态 / ISR
 * - send_timeout / recv_timeout: 任务态
 */

/**
 * @brief 通过 OSAL 统一静态堆创建一个队列。
 * @param length 队列可容纳的消息个数。
 * @param item_size 单个消息项大小，单位为字节。
 * @return 成功返回队列句柄，失败返回 NULL。
 * @note 队列控制块和消息缓冲区都来自 osal_mem 管理的静态堆。
 */
osal_queue_t *osal_queue_create(uint32_t length, uint32_t item_size);

/**
 * @brief 基于用户提供的静态缓冲区创建一个队列。
 * @param buffer 原始队列存储区。
 * @param length 队列可容纳的消息个数。
 * @param item_size 单个消息项大小，单位为字节。
 * @return 成功返回队列句柄，失败返回 NULL。
 * @note 支持固定长度的结构体、指针、数组或它们的组合类型。
 */
osal_queue_t *osal_queue_create_static(void *buffer, uint32_t length, uint32_t item_size);

/**
 * @brief 销毁一个队列控制块。
 * @param q 队列句柄。
 */
void osal_queue_delete(osal_queue_t *q);

/**
 * @brief 查询当前队列内已有多少个消息。
 * @param q 队列句柄。
 * @return 当前已入队的消息数量。
 */
uint32_t osal_queue_get_count(const osal_queue_t *q);

/**
 * @brief 不等待地向队列发送一个消息项。
 * @param q 队列句柄。
 * @param item 源消息指针。
 * @return OSAL 状态码。
 */
osal_status_t osal_queue_send(osal_queue_t *q, const void *item);

/**
 * @brief 不等待地从队列接收一个消息项。
 * @param q 队列句柄。
 * @param item 单个消息项的目标缓冲区。
 * @return OSAL 状态码。
 */
osal_status_t osal_queue_recv(osal_queue_t *q, void *item);

/**
 * @brief 带超时地向队列发送一个消息项。
 * @param q 队列句柄。
 * @param item 源消息指针。
 * @param timeout_ms 超时时间，单位为毫秒。
 * @return OSAL 状态码。
 */
osal_status_t osal_queue_send_timeout(osal_queue_t *q, const void *item, uint32_t timeout_ms);

/**
 * @brief 带超时地从队列接收一个消息项。
 * @param q 队列句柄。
 * @param item 单个消息项的目标缓冲区。
 * @param timeout_ms 超时时间，单位为毫秒。
 * @return OSAL 状态码。
 */
osal_status_t osal_queue_recv_timeout(osal_queue_t *q, void *item, uint32_t timeout_ms);

/**
 * @brief 在中断上下文中向队列发送一个消息项。
 * @param q 队列句柄。
 * @param item 源消息指针。
 * @return OSAL 状态码。
 */
osal_status_t osal_queue_send_from_isr(osal_queue_t *q, const void *item);

/**
 * @brief 在中断上下文中从队列接收一个消息项。
 * @param q 队列句柄。
 * @param item 单个消息项的目标缓冲区。
 * @return OSAL 状态码。
 */
osal_status_t osal_queue_recv_from_isr(osal_queue_t *q, void *item);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_QUEUE_H */
