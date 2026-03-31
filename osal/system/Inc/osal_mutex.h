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
 */

/**
 * @brief 创建一个互斥量对象。
 * @return 成功返回互斥量句柄，失败返回 NULL。
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
 */
osal_status_t osal_mutex_lock(osal_mutex_t *mutex, uint32_t timeout_ms);

/**
 * @brief 解锁一个互斥量。
 * @param mutex 互斥量句柄。
 * @return OSAL 状态码。
 */
osal_status_t osal_mutex_unlock(osal_mutex_t *mutex);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_MUTEX_H */