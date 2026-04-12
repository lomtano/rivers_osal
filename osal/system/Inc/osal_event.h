#ifndef OSAL_EVENT_H
#define OSAL_EVENT_H

#include <stdbool.h>
#include <stdint.h>
#include "osal.h"

#if !OSAL_CFG_ENABLE_EVENT
#error "OSAL event module is disabled. Enable OSAL_CFG_ENABLE_EVENT in osal.h."
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct osal_event osal_event_t;

/*
 * 事件句柄契约：
 * 1. create() 成功后，句柄所有权归调用方。
 * 2. delete(NULL) 是安全空操作。
 * 3. delete() 成功后，句柄立即失效，不能再次 set / clear / wait / delete。
 * 4. debug 打开时，可检测到的非法句柄、重复 delete 会通过 OSAL_DEBUG_HOOK 报告。
 *
 * 接口能力矩阵：
 * - create / delete / wait: 任务态
 * - set / clear: 任务态 / ISR
 * - wait 现在已经采用“等待链表 + BLOCKED + 事件唤醒”的模型
 */

/**
 * @brief 创建一个事件对象。
 * @param auto_reset 为 true 时，在等待成功后自动清除事件。
 * @return 成功返回事件句柄，失败返回 NULL。
 * @note auto_reset=false 时，事件一旦 set 成功，就会一直保持触发状态，直到手动 clear。
 */
osal_event_t *osal_event_create(bool auto_reset);

/**
 * @brief 销毁一个事件对象。
 * @param evt 事件句柄。
 */
void osal_event_delete(osal_event_t *evt);

/**
 * @brief 将事件置为已触发状态。
 * @param evt 事件句柄。
 * @return OSAL 状态码。
 * @note 对 auto_reset 事件来说，这次触发只会被一次 wait 成功消费掉。
 */
osal_status_t osal_event_set(osal_event_t *evt);

/**
 * @brief 将事件清除为未触发状态。
 * @param evt 事件句柄。
 * @return OSAL 状态码。
 */
osal_status_t osal_event_clear(osal_event_t *evt);

/**
 * @brief 等待事件触发或超时。
 * @param evt 事件句柄。
 * @param timeout_ms 超时时间，单位为毫秒。
 * @return
 * - OSAL_OK：事件已触发并被本次 wait 成功消费。
 * - OSAL_ERR_RESOURCE：事件当前未触发，且 timeout_ms=0U，本次不等待。
 * - OSAL_ERR_BLOCKED：当前任务已经进入 BLOCKED，并挂到事件等待链表。
 * - OSAL_ERR_TIMEOUT：等待超时。
 * - OSAL_ERR_DELETED：等待期间事件对象被删除。
 * - 其他状态：参数错误或错误上下文。
 * @note timeout_ms 支持 0U / N / OSAL_WAIT_FOREVER。
 * @note 0U 表示只检查一次当前事件状态，不等待。
 * @note N 毫秒表示最多等待 N 毫秒，超时后返回 OSAL_ERR_TIMEOUT。
 * @note OSAL_WAIT_FOREVER 表示一直等，直到事件被 set 或事件对象被 delete。
 * @note 当事件暂时不可用且允许等待时，当前任务会被置为 BLOCKED，
 *       从普通调度扫描中跳过；事件被 set 后，等待任务会被直接唤醒。
 */
osal_status_t osal_event_wait(osal_event_t *evt, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_EVENT_H */




