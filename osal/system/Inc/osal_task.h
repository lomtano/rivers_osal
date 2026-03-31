#ifndef OSAL_TASK_H
#define OSAL_TASK_H

#include <stdbool.h>
#include "osal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*osal_task_fn_t)(void *arg);
typedef struct osal_task osal_task_t;

/*
 * 任务句柄契约：
 * 1. osal_task_create() 成功后，句柄所有权归调用方。
 * 2. osal_task_delete(NULL) 是安全空操作。
 * 3. osal_task_delete() 成功后，句柄立即失效，不能再次 start / stop / sleep / sleep_until / delete。
 * 4. debug 打开时，可检测到的重复 delete / 非法句柄会通过 OSAL_DEBUG_HOOK 报告。
 *
 * 接口能力矩阵：
 * - create / delete / start / stop / sleep / sleep_until / yield / run: 任务态或主循环
 * - ISR: 不支持
 */

/**
 * @brief 任务优先级。
 * @note 裸机下这里不是抢占式优先级，而是“调度检查优先级”：
 *       高优先级任务每轮先检查，中优先级随后检查，低优先级隔几轮再检查一次，
 *       或者在高/中优先级没有可运行任务时补充检查。
 */
typedef enum {
    OSAL_TASK_PRIORITY_HIGH = 0,
    OSAL_TASK_PRIORITY_MEDIUM = 1,
    OSAL_TASK_PRIORITY_LOW = 2,
    OSAL_TASK_PRIORITY_COUNT
} osal_task_priority_t;

typedef enum {
    OSAL_TASK_READY = 0,
    OSAL_TASK_RUNNING,
    OSAL_TASK_BLOCKED,
    OSAL_TASK_SUSPENDED
} osal_task_state_t;

/**
 * @brief 创建一个协作式任务对象，并指定调度优先级。
 * @param fn 任务入口函数。
 * @param arg 传递给 fn 的用户参数。
 * @param priority 任务调度优先级。
 * @return 成功返回任务句柄，失败返回 NULL。
 */
osal_task_t *osal_task_create(osal_task_fn_t fn, void *arg, osal_task_priority_t priority);

/**
 * @brief 销毁一个任务对象。
 * @param task 任务句柄。
 */
void osal_task_delete(osal_task_t *task);

/**
 * @brief 将任务标记为可运行状态。
 * @param task 任务句柄。
 * @return OSAL 状态码。
 */
osal_status_t osal_task_start(osal_task_t *task);

/**
 * @brief 停止调度某个任务。
 * @param task 任务句柄。
 * @return OSAL 状态码。
 */
osal_status_t osal_task_stop(osal_task_t *task);

/**
 * @brief 让一个任务休眠指定时间。
 * @param task 任务句柄，传 NULL 表示当前任务。
 * @param ms 休眠时长，单位为毫秒。
 * @return OSAL 状态码。
 */
osal_status_t osal_task_sleep(osal_task_t *task, uint32_t ms);

/**
 * @brief 让任务按照内部维护的绝对周期休眠到下一次唤醒点。
 * @param task 任务句柄，传 NULL 表示当前任务。
 * @param period_ms 周期间隔，单位为毫秒。
 * @return OSAL 状态码。
 * @note 该接口适合实现稳定周期任务，语义类似 FreeRTOS 的 vTaskDelayUntil，
 *       但节拍参考点由 OSAL 在任务对象内部自动维护，调用方无需额外保存静态变量。
 */
osal_status_t osal_task_sleep_until(osal_task_t *task, uint32_t period_ms);

/**
 * @brief 主动让出执行权，给同级或其他级任务运行机会。
 */
void osal_task_yield(void);

/**
 * @brief 执行一轮协作式调度。
 */
void osal_run(void);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_TASK_H */
