#ifndef OSAL_MUTEX_H
#define OSAL_MUTEX_H

#include <stdint.h>
#include "osal.h"

#if !OSAL_CFG_ENABLE_MUTEX
#error "OSAL mutex module is disabled. Enable OSAL_CFG_ENABLE_MUTEX in osal.h."
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct osal_mutex osal_mutex_t;

/*
 * 互斥量句柄契约：
 * 1. create() 成功后，句柄所有权归调用方。
 * 2. delete(NULL) 是安全空操作。
 * 3. delete() 成功后，句柄立即失效，不能再次 lock / unlock / delete。
 * 4. debug 打开时，可检测到的非法句柄、重复 delete 会通过 OSAL_DEBUG_HOOK 报告。
 *
 * 接口能力矩阵：
 * - create / delete / lock / unlock: 任务态
 * - ISR: 不支持
 * - lock 现在已经采用“等待链表 + BLOCKED + unlock 唤醒”的模型
 * - 当前仍然没有 owner 检查，也没有优先级继承
 */

/**
 * @brief 创建一个互斥量对象。
 * @return 成功返回互斥量句柄，失败返回 NULL。
 * @note 当前实现是最小互斥量，不带 owner 信息。
 */
osal_mutex_t *osal_mutex_create(void);

/**
 * @brief 销毁一个互斥量对象。
 * @param mutex 互斥量句柄。
 */
void osal_mutex_delete(osal_mutex_t *mutex);

/**
 * @brief 带超时地锁定一个互斥量。
 * @param mutex 互斥量句柄。
 * @param timeout_ms 超时时间，单位为毫秒。
 * @return OSAL 状态码。
 * @note timeout_ms 支持 0U / N / OSAL_WAIT_FOREVER。
 * @note 0U 表示只尝试一次，拿不到锁就立刻返回 OSAL_ERR_TIMEOUT。
 * @note N 毫秒表示最多等待 N 毫秒，超时后返回 OSAL_ERR_TIMEOUT。
 * @note OSAL_WAIT_FOREVER 表示一直等，直到别的任务 unlock 或互斥量被 delete。
 * @note 当前实现只保证最基本的互斥访问，不提供 owner 检查和优先级继承。
 * @note 当锁暂时不可用且允许等待时，当前任务会被置为 BLOCKED，
 *       从普通调度扫描里跳过；unlock 后等待任务会被直接唤醒。
 */
osal_status_t osal_mutex_lock(osal_mutex_t *mutex, uint32_t timeout_ms);

/**
 * @brief 解锁一个互斥量。
 * @param mutex 互斥量句柄。
 * @return OSAL 状态码。
 * @note 当前实现不检查“是不是同一个任务解的锁”。这是最小实现的边界之一。
 */
osal_status_t osal_mutex_unlock(osal_mutex_t *mutex);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_MUTEX_H */




