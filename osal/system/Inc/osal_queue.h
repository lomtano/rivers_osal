#ifndef OSAL_QUEUE_H
#define OSAL_QUEUE_H

#include <stdint.h>
#include "osal.h"
#include "osal_timer.h" /* 提供超时等待所需的时基辅助函数。 */
#include "osal_irq.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct osal_queue osal_queue_t;

/**
 * @brief 通过 OSAL 统一静态堆创建一个队列。
 * @param length 队列可容纳的消息个数。
 * @param item_size 单个消息项大小，单位为字节。
 * @return 成功返回队列句柄，失败返回 NULL。
 * @note 该接口分配的是 osal_mem 管理的静态内存，不依赖 libc 或系统堆。
 */
osal_queue_t *osal_queue_create(uint32_t length, uint32_t item_size);

/**
 * @brief 基于用户提供的静态缓冲区创建一个队列。
 * @param buffer 原始队列存储区。
 * @param length 队列可容纳的消息个数。
 * @param item_size 单个消息项大小，单位为字节。
 * @return 成功返回队列句柄，失败返回 NULL。
 * @note 该接口支持任意固定长度消息类型，包括结构体、指针、定长数组以及它们的组合。
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

/* 非阻塞发送与接收接口。 */
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

/* 带毫秒超时的发送与接收接口。 */
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

/* 中断上下文使用的接口。 */
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
