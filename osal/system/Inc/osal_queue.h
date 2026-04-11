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

/* 队列控制块前向声明。
 * 用户只拿句柄，不直接访问内部环形缓冲区和等待链表。 */
typedef struct osal_queue osal_queue_t;

/*
 * 队列句柄使用约定
 * ---------------------------------------------------------------------------
 * 1. osal_queue_create() 和 osal_queue_create_static() 成功后，
 *    返回的队列句柄归调用方持有。
 * 2. osal_queue_delete(NULL) 是安全空操作。
 * 3. delete() 成功后，句柄立即失效，不能再继续 send / recv / get_count。
 * 4. create() 模式下，消息缓冲区来自 OSAL 统一内存池。
 * 5. create_static() 模式下，消息缓冲区由用户提供，但队列控制块仍来自 OSAL。
 * 6. 如果 debug 打开，重复 delete、非法句柄等异常会尽量通过
 *    OSAL_DEBUG_HOOK 给出诊断。
 *
 * 接口能力矩阵
 * ---------------------------------------------------------------------------
 * 1. create / create_static / delete：任务态或初始化阶段使用。
 * 2. get_count / send / recv：任务态使用。
 * 3. send_timeout / recv_timeout：任务态使用，因为它们可能把当前任务挂起。
 * 4. send_from_isr / recv_from_isr：ISR 专用。
 *
 * 统一等待语义
 * ---------------------------------------------------------------------------
 * 1. timeout_ms = 0U
 *    表示不等待，资源不满足就立刻返回。
 * 2. timeout_ms = N
 *    表示最多等待 N 毫秒。
 * 3. timeout_ms = OSAL_WAIT_FOREVER
 *    表示永久等待，直到资源满足。
 */

/**
 * @brief 从 OSAL 统一内存池中创建一个队列。
 *
 * @param length 队列最多可以容纳多少个消息项。
 * @param item_size 每个消息项占多少字节。
 *
 * @return 成功返回队列句柄，失败返回 NULL。
 *
 * @note 这个接口适合“我只想赶紧把队列用起来，不想自己准备缓冲区”的场景。
 * @note 队列控制块和消息缓冲区都来自 osal_mem。
 */
osal_queue_t *osal_queue_create(uint32_t length, uint32_t item_size);

/**
 * @brief 用用户提供的静态缓冲区创建一个队列。
 *
 * @param buffer 用户提供的消息存储区首地址。
 * @param length 队列最多容纳多少个消息项。
 * @param item_size 每个消息项占多少字节。
 *
 * @return 成功返回队列句柄，失败返回 NULL。
 *
 * @note 适合“我想自己控制消息缓冲区放在哪段内存里”的场景。
 * @note 队列项可以是：
 *       1. 固定长度结构体
 *       2. 指针
 *       3. 定长数组
 *       4. 以上几种的组合结构体
 *
 * @note 这个队列是“固定项大小队列”，不是可变长消息队列。
 */
osal_queue_t *osal_queue_create_static(void *buffer, uint32_t length, uint32_t item_size);

/**
 * @brief 删除一个队列对象。
 *
 * @param q 队列句柄。
 *
 * @note 如果 q 为 NULL，函数直接返回。
 * @note 删除成功后，队列句柄和其内部状态都视为失效。
 */
void osal_queue_delete(osal_queue_t *q);

/**
 * @brief 查询当前队列里已有多少条消息。
 *
 * @param q 队列句柄。
 *
 * @return 当前已入队消息数。
 *
 * @note 这个值适合做调试、状态监控，不建议拿它替代 send/recv 的实际返回值。
 */
uint32_t osal_queue_get_count(const osal_queue_t *q);

/**
 * @brief 非阻塞发送一个消息项。
 *
 * @param q 队列句柄。
 * @param item 指向待发送消息的源地址。
 *
 * @return
 * - OSAL_OK：发送成功。
 * - OSAL_ERR_RESOURCE：队列已满。
 * - 其他状态：参数错误或句柄非法。
 *
 * @note 这个接口不会等待。
 *       如果队列已满，它直接返回，不会把当前任务挂起。
 */
osal_status_t osal_queue_send(osal_queue_t *q, const void *item);

/**
 * @brief 非阻塞接收一个消息项。
 *
 * @param q 队列句柄。
 * @param item 指向接收缓冲区。
 *
 * @return
 * - OSAL_OK：接收成功。
 * - OSAL_ERR_RESOURCE：队列为空。
 * - 其他状态：参数错误或句柄非法。
 *
 * @note 这个接口也不会等待。
 *       如果队列为空，它直接返回。
 */
osal_status_t osal_queue_recv(osal_queue_t *q, void *item);

/**
 * @brief 按统一等待语义发送一个消息项。
 *
 * @param q 队列句柄。
 * @param item 指向待发送消息的源地址。
 * @param timeout_ms 等待时长，支持 0U / N / OSAL_WAIT_FOREVER。
 *
 * @return
 * - OSAL_OK：发送成功。
 * - OSAL_ERR_TIMEOUT：等待超时。
 * - OSAL_ERR_RESOURCE：当前轮还未满足资源，任务已经被挂起或本次不等待失败。
 * - 其他状态：参数错误、非法句柄或错误上下文。
 *
 * @note 这个接口是当前队列最重要的使用方式之一。
 *       它的真实行为是：
 *       1. 如果队列有空位，立刻发送并返回 OSAL_OK。
 *       2. 如果 timeout_ms 为 0U，立刻返回，不挂起任务。
 *       3. 如果 timeout_ms 为 N 或 OSAL_WAIT_FOREVER，则当前任务进入 BLOCKED，
 *          并被挂到“等待可写”链表中。
 *       4. 当别的任务或 ISR 从这个队列取走消息后，队列出现空位，
 *          OSAL 会主动把等待发送的任务置为 READY。
 *
 * @note 在协作式任务函数里，推荐写法是：
 *       1. 只有返回 OSAL_OK 才继续处理发送成功后的逻辑。
 *       2. 如果返回的不是 OSAL_OK，本轮任务函数直接 return，
 *          等调度器下次再次进入。
 */
osal_status_t osal_queue_send_timeout(osal_queue_t *q, const void *item, uint32_t timeout_ms);

/**
 * @brief 按统一等待语义接收一个消息项。
 *
 * @param q 队列句柄。
 * @param item 指向接收缓冲区。
 * @param timeout_ms 等待时长，支持 0U / N / OSAL_WAIT_FOREVER。
 *
 * @return
 * - OSAL_OK：接收成功。
 * - OSAL_ERR_TIMEOUT：等待超时。
 * - OSAL_ERR_RESOURCE：当前轮还未收到消息，任务已经被挂起或本次不等待失败。
 * - 其他状态：参数错误、非法句柄或错误上下文。
 *
 * @note 这个接口与 send_timeout() 的思路完全对应：
 *       1. 有消息时立刻复制到 item 并返回。
 *       2. 没消息且 timeout_ms = 0U 时直接返回。
 *       3. 没消息且允许等待时，把当前任务挂入“等待可读”链表。
 *       4. 当别的任务或 ISR 往队列里放入消息后，等待接收的任务会被直接唤醒。
 *
 * @note 这就是当前队列模块“事件驱动”的关键：
 *       不是每次轮询都盲扫消息是否到来，而是资源变化时主动唤醒等待者。
 */
osal_status_t osal_queue_recv_timeout(osal_queue_t *q, void *item, uint32_t timeout_ms);

/**
 * @brief 在 ISR 中向队列发送一个消息项。
 *
 * @param q 队列句柄。
 * @param item 指向待发送消息的源地址。
 *
 * @return OSAL 状态码。
 *
 * @note 这个接口不会让 ISR 阻塞等待。
 * @note 如果发送成功，OSAL 会尝试把等待接收该队列的任务直接置为 READY。
 */
osal_status_t osal_queue_send_from_isr(osal_queue_t *q, const void *item);

/**
 * @brief 在 ISR 中从队列接收一个消息项。
 *
 * @param q 队列句柄。
 * @param item 指向接收缓冲区。
 *
 * @return OSAL 状态码。
 *
 * @note 这个接口同样不会在 ISR 中等待。
 * @note 如果接收成功，OSAL 会尝试把等待向该队列发送消息的任务直接置为 READY。
 */
osal_status_t osal_queue_recv_from_isr(osal_queue_t *q, void *item);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_QUEUE_H */
