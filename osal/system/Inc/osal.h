#ifndef OSAL_H
#define OSAL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OSAL_OK = 0,
    OSAL_ERROR = 1,
    OSAL_ERR_TIMEOUT = 2,
    OSAL_ERR_RESOURCE = 3,
    OSAL_ERR_PARAM = 4,
    OSAL_ERR_NOMEM = 5,
    OSAL_ERR_ISR = 6,
    OSAL_RESERVED = 0x7FFFFFFF
} osal_status_t;

/*
 * ---------------------------------------------------------------------------
 * OSAL 功能裁剪配置
 * ---------------------------------------------------------------------------
 * 1. 默认全部开启，开箱即可使用。
 * 2. mem / irq / task / 基础 timer 时基 属于内核核心能力，默认常开。
 * 3. queue / event / mutex / 软件定时器 / USART 组件 / Flash 组件 属于可选件。
 * 4. 如需裁剪，请在包含 osal.h 之前改写这些宏，或直接在本文件中修改默认值。
 */
#ifndef OSAL_CFG_ENABLE_QUEUE
#define OSAL_CFG_ENABLE_QUEUE 1
#endif

#ifndef OSAL_CFG_ENABLE_EVENT
#define OSAL_CFG_ENABLE_EVENT 1
#endif

#ifndef OSAL_CFG_ENABLE_MUTEX
#define OSAL_CFG_ENABLE_MUTEX 1
#endif

#ifndef OSAL_CFG_ENABLE_SW_TIMER
#define OSAL_CFG_ENABLE_SW_TIMER 1
#endif

#ifndef OSAL_CFG_ENABLE_USART
#define OSAL_CFG_ENABLE_USART 1
#endif

#ifndef OSAL_CFG_ENABLE_FLASH
#define OSAL_CFG_ENABLE_FLASH 1
#endif

/*
 * ---------------------------------------------------------------------------
 * OSAL 诊断配置
 * ---------------------------------------------------------------------------
 * 1. OSAL_CFG_ENABLE_DEBUG = 0 时，诊断宏全部为空操作。
 * 2. OSAL_CFG_ENABLE_DEBUG = 1 时，OSAL_DEBUG_HOOK(module, message) 会在可检测的
 *    非法句柄、重复释放、重复绑定、错误上下文调用等场景下被触发。
 * 3. 推荐在应用层定义类似下面的钩子，再包含 osal.h：
 *      #define OSAL_CFG_ENABLE_DEBUG 1
 *      #define OSAL_DEBUG_HOOK(module, message) \
 *          printf("[OSAL/%s] %s\r\n", module, message)
 * 4. 如果控制台尚未挂载，钩子即使被触发也可能没有可见输出，这属于预期行为。
 */
#ifndef OSAL_CFG_ENABLE_DEBUG
#define OSAL_CFG_ENABLE_DEBUG 0
#endif

#ifndef OSAL_DEBUG_HOOK
#define OSAL_DEBUG_HOOK(module, message) printf("[OSAL/%s] %s\r\n", module, message)
#endif

#if OSAL_CFG_ENABLE_DEBUG
#define OSAL_DEBUG_REPORT(module, message) do { OSAL_DEBUG_HOOK((module), (message)); } while (0)
#else
#define OSAL_DEBUG_REPORT(module, message) ((void)0)
#endif

/*
 * ---------------------------------------------------------------------------
 * OSAL 句柄资源契约
 * ---------------------------------------------------------------------------
 * 1. create / alloc 成功后，资源所有权归调用方。
 * 2. delete / destroy / free 成功后，原句柄或指针立即失效，不允许继续使用。
 * 3. delete(NULL) / destroy(NULL) / free(NULL) 默认视为安全空操作。
 * 4. 重复 delete、重复 destroy、陈旧句柄访问属于调用方错误。
 * 5. release 构建下，OSAL 优先保持轻量，能静默返回的地方通常会静默返回。
 * 6. debug 构建下，凡是实现层能够检测到的重复释放、重复绑定、非法句柄、
 *    错误上下文调用，都会通过 OSAL_DEBUG_HOOK 给出诊断信息。
 * 7. 只有名字显式带 from_isr，或头文件能力矩阵明确标注“任务态/ISR”的接口，
 *    才建议在 ISR 中使用。
 */

/**
 * @brief 初始化 OSAL 系统层。
 * @note 该接口会调用平台层初始化钩子，并自动同步当前 Tick 计数源配置。
 */
void osal_init(void);

/**
 * @brief 在周期性 Tick 中断里调用的 OSAL 通用中断入口。
 * @note 推荐直接在 SysTick_Handler() 或其他系统时基中断中调用它。
 */
void osal_tick_handler(void);

#include "osal_task.h"
#include "osal_mem.h"
#include "osal_irq.h"
#include "osal_timer.h"
#include "osal_platform.h"

#if OSAL_CFG_ENABLE_QUEUE
#include "osal_queue.h"
#endif

#if OSAL_CFG_ENABLE_EVENT
#include "osal_event.h"
#endif

#if OSAL_CFG_ENABLE_MUTEX
#include "osal_mutex.h"
#endif

#ifdef __cplusplus
}
#endif

#endif /* OSAL_H */
