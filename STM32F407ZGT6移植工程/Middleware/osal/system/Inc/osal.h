#ifndef OSAL_H
#define OSAL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 这个头文件是 OSAL 的总入口。
 * ---------------------------------------------------------------------------
 * 1. 应用层通常只需要包含它，不需要分别包含 task.h / queue.h / timer.h。
 * 2. 它先给出最基础的状态码、等待语义、功能开关和调试开关。
 * 3. 然后再按统一顺序聚合各个内核模块和组件模块的头文件。
 * 4. 对当前工程来说，main.c 只包含 osal.h 即可开始使用 OSAL。
 */

typedef enum {
    OSAL_OK = 0,
    OSAL_ERROR = 1,
    OSAL_ERR_TIMEOUT = 2,
    OSAL_ERR_RESOURCE = 3,
    OSAL_ERR_PARAM = 4,
    OSAL_ERR_NOMEM = 5,
    OSAL_ERR_ISR = 6,
    OSAL_ERR_BLOCKED = 7,
    OSAL_ERR_DELETED = 8,
    OSAL_RESERVED = 0x7FFFFFFF
} osal_status_t;

/*
 * 状态码约定补充：
 * 1. OSAL_ERR_RESOURCE
 *    资源当前不可用，但这次调用没有进入阻塞等待。
 *    典型场景：
 *    - 非阻塞 queue send 时队列已满
 *    - 非阻塞 queue recv 时队列为空
 *    - event/mutex 在 timeout=0U 时条件不满足
 * 2. OSAL_ERR_BLOCKED
 *    当前任务已经被挂到等待链表，并切到 BLOCKED。
 *    这不是最终失败，而是“本轮 API 调用先结束，等待后续唤醒”。
 * 3. OSAL_ERR_TIMEOUT
 *    任务曾经进入等待，最终因超时恢复。
 * 4. OSAL_ERR_DELETED
 *    任务曾经进入等待，但等待对象在等待期间被删除。
 */

/*
 * ---------------------------------------------------------------------------
 * OSAL 统一等待语义
 * ---------------------------------------------------------------------------
 * 1. timeout = 0U
 *    表示不等待，资源不满足就立刻返回。
 * 2. timeout = N
 *    表示最多等待 N 毫秒。
 * 3. timeout = OSAL_WAIT_FOREVER
 *    表示永久等待，直到资源满足或被显式取消。
 *
 * 这个约定会在 queue / event / mutex 等需要“等待资源”的接口里重复出现。
 */
#ifndef OSAL_WAIT_FOREVER
#define OSAL_WAIT_FOREVER 0xFFFFFFFFUL
#endif

/*
 * ---------------------------------------------------------------------------
 * OSAL 功能裁剪配置
 * ---------------------------------------------------------------------------
 * 1. 默认全部开启，开箱即可使用。
 * 2. mem / irq / task / 基础 timer 时基 属于内核核心能力，默认常开。
 * 3. queue / event / mutex / 软件定时器 / USART 组件 / Flash 组件 属于可选件。
 * 4. 如需裁剪，请在包含 osal.h 之前改写这些宏，或直接在本文件中修改默认值。
 * 5. 这些开关的作用不仅是裁剪代码体积，也能明确告诉读者：
 *    “哪些是内核必需件，哪些是可选件”。
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
 * OSAL 调试配置
 * ---------------------------------------------------------------------------
 * 1. OSAL_CFG_ENABLE_DEBUG = 0 时，调试宏全部为空操作。
 * 2. OSAL_CFG_ENABLE_DEBUG = 1 时，OSAL_DEBUG_HOOK(module, message) 会在可检测的
 *    非法句柄、重复释放、重复绑定、错误上下文调用等场景下被触发。
 * 3. 推荐在应用层定义类似下面的钩子，再包含 osal.h：
 *      #define OSAL_CFG_ENABLE_DEBUG 1
 *      #define OSAL_DEBUG_HOOK(module, message) \
 *          my_debug_output((module), (message))
 * 4. OSAL 默认不绑定任何输出后端；RTT、USART、半主机等均由用户自行决定。
 * 5. 如果你没有自定义 OSAL_DEBUG_HOOK，即使打开 debug，系统层也不会主动输出。
 * 6. 这样设计的目的，是让 system 层不偷偷依赖 printf、串口或 RTT。
 */
#ifndef OSAL_CFG_ENABLE_DEBUG
#define OSAL_CFG_ENABLE_DEBUG 0
#endif

#ifndef OSAL_DEBUG_HOOK
#define OSAL_DEBUG_HOOK(module, message) ((void)0)
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
 * 3. delete(NULL) / destroy(NULL) / free(NULL) 默认视为空操作。
 * 4. 重复 delete、重复 destroy、陈旧句柄访问属于调用方错误。
 * 5. release 构建下，OSAL 优先保持轻量，能静默返回的地方通常会静默返回。
 * 6. debug 构建下，凡是实现层能够检测到的重复释放、重复绑定、非法句柄、
 *    错误上下文调用，都会通过 OSAL_DEBUG_HOOK 给出诊断信息。
 * 7. 只有名字显式带 from_isr，或头文件能力矩阵明确标注“任务态 / ISR”的接口，
 *    才建议在 ISR 中使用。
 *
 * 这一段契约的目的，是把“资源怎么创建、何时失效、哪些情况属于调用方错误”
 * 提前写清楚，避免用户靠猜来使用接口。
 */

/**
 * @brief 初始化 OSAL 系统层。
 *
 * @note 这个接口通常在 main() 的硬件初始化之后调用一次。
 *
 * @note 它会负责：
 *       1. 初始化平台抽象层。
 *       2. 配置并启动默认系统时基。
 *       3. 初始化 timer 子系统使用的 Tick 原始来源。
 *
 * @note 对当前工程来说，用户通常不需要再手动配置 SysTick。
 */
void osal_init(void);

/**
 * @brief 在周期性 Tick 中断里调用的 OSAL 通用中断入口。
 *
 * @note 推荐直接在 SysTick_Handler() 或其他系统时基中断中调用它。
 * @note 它本身不做复杂调度，只负责把“时间过去了一次”这件事通知给 OSAL。
 */
void osal_tick_handler(void);

/* 内核核心头文件。
 * 这几个模块构成 OSAL 的最基础运行骨架。 */
#include "osal_task.h"
#include "osal_mem.h"
#include "osal_irq.h"
#include "osal_timer.h"
#include "osal_platform.h"

/* 可选同步原语。
 * 这些模块能通过功能开关裁剪。 */
#if OSAL_CFG_ENABLE_QUEUE
#include "osal_queue.h"
#endif

#if OSAL_CFG_ENABLE_EVENT
#include "osal_event.h"
#endif

#if OSAL_CFG_ENABLE_MUTEX
#include "osal_mutex.h"
#endif

#if OSAL_CFG_ENABLE_USART
#include "periph_uart.h"
#endif

#if OSAL_CFG_ENABLE_FLASH
#include "periph_flash.h"
#endif

/*
 * ---------------------------------------------------------------------------
 * OSAL 应用层便利聚合
 * ---------------------------------------------------------------------------
 * 1. 对当前工程来说，main.c 只包含 osal.h 就够了。
 * 2. 下面这个宏默认指向当前板子的具体适配头文件。
 * 3. 你移植到别的板子时，只需要把它改成新的平台头文件名即可。
 * 4. 如果你不想让 osal.h 自动聚合平台头，也可以把
 *    OSAL_CFG_INCLUDE_PLATFORM_HEADER 改成 0。
 * 5. 这样做的目的，是让应用层入口文件尽量简洁，减少“到底还要 include 哪些头”
 *    这种和业务无关的记忆负担。
 */
#ifndef OSAL_CFG_INCLUDE_PLATFORM_HEADER
#define OSAL_CFG_INCLUDE_PLATFORM_HEADER 1
#endif

#ifndef OSAL_PLATFORM_HEADER_FILE
#define OSAL_PLATFORM_HEADER_FILE "osal_platform_stm32f4.h"
#endif

#if OSAL_CFG_INCLUDE_PLATFORM_HEADER
#include OSAL_PLATFORM_HEADER_FILE
#endif

#ifdef __cplusplus
}
#endif

#endif /* OSAL_H */




