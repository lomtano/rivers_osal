#ifndef OSAL_H
#define OSAL_H

#include "osal_config.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * OSAL 总入口。
 * 1. 应用层通常只需要包含这个头文件。
 * 2. 配置宏统一来自 osal_config.h。
 * 3. 这里先定义状态码和调试宏，再聚合各模块头文件。
 */

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
 * 状态码约定：
 * 1. OSAL_OK
 *    本次调用已经完成预期动作。
 * 2. OSAL_ERR_RESOURCE
 *    资源当前不可用，但本次调用没有进入等待。
 *    典型场景：
 *    - 非阻塞 queue send 时队列已满
 *    - 非阻塞 queue recv 时队列为空
 * 3. OSAL_ERR_TIMEOUT
 *    调用在允许的时间窗口内仍未完成。
 * 4. OSAL_ERR_PARAM
 *    句柄、指针、长度或其他输入参数不合法。
 * 5. OSAL_ERR_NOMEM
 *    统一内存池无法再提供所需内存。
 * 6. OSAL_ERR_ISR
 *    当前接口不允许在 ISR 上下文中调用。
 */

/*
 * 调试配置：
 * 1. debug 打开时，OSAL 会在可检测到的非法句柄、错误上下文调用等场景触发钩子。
 * 2. 如果用户没有自定义 OSAL_DEBUG_HOOK，默认不输出任何内容。
 */
#ifndef OSAL_DEBUG_HOOK
#define OSAL_DEBUG_HOOK(module, message) ((void)0)
#endif

#if OSAL_CFG_ENABLE_DEBUG
#define OSAL_DEBUG_REPORT(module, message) do { OSAL_DEBUG_HOOK((module), (message)); } while (0)
#else
#define OSAL_DEBUG_REPORT(module, message) ((void)0)
#endif

/*
 * 顶层调度循环空闲钩子：
 * 1. 默认不做任何事。
 * 2. 如果应用要做低功耗，可在这里接入 __WFI() 或板级 idle 处理。
 * 3. 这个钩子由 osal_start_system() 在每轮协作式调度后调用；
 *    如果你的任务需要持续高速轮询，不要在这里无条件睡眠。
 */
#ifndef OSAL_IDLE_HOOK
#define OSAL_IDLE_HOOK() ((void)0)
#endif

/*
 * 句柄资源契约：
 * 1. create / alloc 成功后，资源所有权归调用方。
 * 2. delete / destroy / free 成功后，原句柄或指针立即失效。
 * 3. delete(NULL) / destroy(NULL) / free(NULL) 默认视为空操作。
 * 4. release 构建下优先保持轻量；debug 构建下尽量报告可检测的错误使用方式。
 */

/**
 * @brief 初始化 OSAL 内核及其底层平台桥接。
 * @note 当前默认流程会依次完成平台初始化钩子、中断控制器配置、IRQ profiling 初始化、
 *       SysTick 配置以及 timer 子系统的 Tick 源同步。
 */
void osal_init(void);

/**
 * @brief 在系统 Tick 中断里推进 OSAL 时间基准。
 * @note 推荐直接在 `SysTick_Handler()` 或等价系统时基中断里调用。
 * @note 该函数只做最小时间累计，不负责调度任务。
 */
void osal_tick_handler(void);

#include "osal_task.h"
#include "osal_mem.h"
#include "osal_irq.h"
#include "osal_timer.h"
#include "osal_cortexm.h"

#if OSAL_CFG_ENABLE_QUEUE
#include "osal_queue.h"
#endif

#if OSAL_CFG_ENABLE_USART
#include "periph_uart.h"
#endif

#if OSAL_CFG_ENABLE_FLASH
#include "periph_flash.h"
#endif

#if OSAL_CFG_INCLUDE_PLATFORM_HEADER
#include OSAL_PLATFORM_HEADER_FILE
#endif

#ifdef __cplusplus
}
#endif

#endif /* OSAL_H */
