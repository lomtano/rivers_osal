#ifndef OSAL_PLATFORM_H
#define OSAL_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ---------------------------------------------------------------------------
 * OSAL 平台抽象说明
 * ---------------------------------------------------------------------------
 * 1. 本头文件属于 system 层，用来放 OSAL 内核真正依赖的平台抽象。
 * 2. Tick、SysTick、IRQ 这些属于 OSAL 核心时基与临界区能力，因此放在这里。
 * 3. UART、Flash、LED 这类板级外设桥接，不放在这里，仍然留给 platform/example。
 * 4. 这样可以保证：
 *    - system 负责 OSAL 内核逻辑
 *    - platform 负责 MCU 外设桥接和示例适配
 *    - 依赖方向始终是“适配层依赖 OSAL”，而不是反过来
 */

/*
 * ---------------------------------------------------------------------------
 * 用户配置区 1：Cortex-M SysTick 基本参数
 * ---------------------------------------------------------------------------
 * 1. 默认按 Cortex-M 内核自带 SysTick 设计。
 * 2. 这里主要给用户填写 CPU 时钟和目标 Tick 频率。
 * 3. osal_init() 会自动完成：
 *    - 中断分组配置
 *    - SysTick 优先级配置
 *    - SysTick 重装值 / 使能 / 中断开关配置
 */
#ifndef OSAL_PLATFORM_CPU_CLOCK_HZ
#define OSAL_PLATFORM_CPU_CLOCK_HZ 168000000UL
#endif

#ifndef OSAL_PLATFORM_SYSTICK_CLOCK_HZ
#define OSAL_PLATFORM_SYSTICK_CLOCK_HZ OSAL_PLATFORM_CPU_CLOCK_HZ
#endif

#ifndef OSAL_PLATFORM_TICK_RATE_HZ
#define OSAL_PLATFORM_TICK_RATE_HZ 1000UL
#endif

#ifndef OSAL_PLATFORM_SYSTICK_USE_CORE_CLOCK
#define OSAL_PLATFORM_SYSTICK_USE_CORE_CLOCK 1U
#endif

#ifndef OSAL_PLATFORM_SYSTICK_HANDLER_NAME
#define OSAL_PLATFORM_SYSTICK_HANDLER_NAME SysTick_Handler
#endif

/*
 * ---------------------------------------------------------------------------
 * 用户配置区 2：中断分组与 SysTick 优先级
 * ---------------------------------------------------------------------------
 * 1. 默认行为对齐常见 FreeRTOS Cortex-M 移植：
 *    - 优先级分组：Group 4（4bit 抢占优先级，0bit 子优先级）
 *    - SysTick 优先级：最低优先级
 * 2. 如果工程希望自己在别处配置分组或优先级，可以把下面两个开关改成 0。
 * 3. 这里使用 Cortex-M AIRCR 原始 PRIGROUP 字段值。
 *    对于 STM32F4 来说，Group 4 对应的原始值是 3。
 */
#ifndef OSAL_PLATFORM_CONFIGURE_PRIORITY_GROUP
#define OSAL_PLATFORM_CONFIGURE_PRIORITY_GROUP 1U
#endif

#ifndef OSAL_PLATFORM_NVIC_PRIORITY_GROUP_RAW
#define OSAL_PLATFORM_NVIC_PRIORITY_GROUP_RAW 3U
#endif

#ifndef OSAL_PLATFORM_CONFIGURE_SYSTICK_PRIORITY
#define OSAL_PLATFORM_CONFIGURE_SYSTICK_PRIORITY 1U
#endif

#ifndef OSAL_PLATFORM_NVIC_PRIO_BITS
#define OSAL_PLATFORM_NVIC_PRIO_BITS 4U
#endif

#ifndef OSAL_PLATFORM_SYSTICK_PRIORITY_LEVEL
#define OSAL_PLATFORM_SYSTICK_PRIORITY_LEVEL ((1UL << OSAL_PLATFORM_NVIC_PRIO_BITS) - 1UL)
#endif

/*
 * ---------------------------------------------------------------------------
 * 用户配置区 3：SysTick / NVIC 原始寄存器映射
 * ---------------------------------------------------------------------------
 * 1. 默认值直接对应 Cortex-M 标准 SysTick 地址。
 * 2. 对 STM32 / GD32 / N32 这类 Cortex-M MCU，通常无需修改。
 * 3. system 层只读这些原始值，复杂换算仍在 osal_timer.c 里完成。
 * 4. AIRCR / SHPR3 也在这里定义，是为了让 OSAL 自动配置优先级分组和 SysTick 优先级。
 */
#ifndef OSAL_PLATFORM_SCB_AIRCR_REG
#define OSAL_PLATFORM_SCB_AIRCR_REG (*((volatile uint32_t *)0xE000ED0CUL))
#endif

#ifndef OSAL_PLATFORM_SCB_SHPR3_REG
#define OSAL_PLATFORM_SCB_SHPR3_REG (*((volatile uint32_t *)0xE000ED20UL))
#endif

#ifndef OSAL_PLATFORM_SYSTICK_CTRL_REG
#define OSAL_PLATFORM_SYSTICK_CTRL_REG (*((volatile uint32_t *)0xE000E010UL))
#endif

#ifndef OSAL_PLATFORM_SYSTICK_LOAD_REG
#define OSAL_PLATFORM_SYSTICK_LOAD_REG (*((volatile uint32_t *)0xE000E014UL))
#endif

#ifndef OSAL_PLATFORM_SYSTICK_CURRENT_VALUE_REG
#define OSAL_PLATFORM_SYSTICK_CURRENT_VALUE_REG (*((volatile uint32_t *)0xE000E018UL))
#endif

#ifndef OSAL_PLATFORM_SYSTICK_CLK_BIT
#define OSAL_PLATFORM_SYSTICK_CLK_BIT (1UL << 2UL)
#endif

#ifndef OSAL_PLATFORM_SYSTICK_INT_BIT
#define OSAL_PLATFORM_SYSTICK_INT_BIT (1UL << 1UL)
#endif

#ifndef OSAL_PLATFORM_SYSTICK_ENABLE_BIT
#define OSAL_PLATFORM_SYSTICK_ENABLE_BIT (1UL << 0UL)
#endif

#ifndef OSAL_PLATFORM_SYSTICK_COUNTFLAG_BIT
#define OSAL_PLATFORM_SYSTICK_COUNTFLAG_BIT (1UL << 16UL)
#endif

/*
 * ---------------------------------------------------------------------------
 * 用户配置区 4：NVIC / AIRCR 位定义
 * ---------------------------------------------------------------------------
 * 1. 默认值对应 Cortex-M 内核寄存器位定义。
 * 2. 如果目标核实现不同，用户只需要在这里覆写宏即可。
 */
#ifndef OSAL_PLATFORM_AIRCR_VECTKEY_POS
#define OSAL_PLATFORM_AIRCR_VECTKEY_POS 16U
#endif

#ifndef OSAL_PLATFORM_AIRCR_VECTKEY_MASK
#define OSAL_PLATFORM_AIRCR_VECTKEY_MASK (0xFFFFUL << OSAL_PLATFORM_AIRCR_VECTKEY_POS)
#endif

#ifndef OSAL_PLATFORM_AIRCR_VECTKEY
#define OSAL_PLATFORM_AIRCR_VECTKEY 0x5FAUL
#endif

#ifndef OSAL_PLATFORM_AIRCR_PRIGROUP_POS
#define OSAL_PLATFORM_AIRCR_PRIGROUP_POS 8U
#endif

#ifndef OSAL_PLATFORM_AIRCR_PRIGROUP_MASK
#define OSAL_PLATFORM_AIRCR_PRIGROUP_MASK (0x7UL << OSAL_PLATFORM_AIRCR_PRIGROUP_POS)
#endif

#ifndef OSAL_PLATFORM_SHPR3_SYSTICK_POS
#define OSAL_PLATFORM_SHPR3_SYSTICK_POS 24U
#endif

#ifndef OSAL_PLATFORM_SHPR3_SYSTICK_MASK
#define OSAL_PLATFORM_SHPR3_SYSTICK_MASK (0xFFUL << OSAL_PLATFORM_SHPR3_SYSTICK_POS)
#endif

/*
 * ---------------------------------------------------------------------------
 * 用户配置区 5：IRQ 与上下文判断宏
 * ---------------------------------------------------------------------------
 * 1. 这里保留“宏替换”风格，方便你把 CMSIS 或厂商 SDK 的接口挂进来。
 * 2. 默认实现现在直接使用 CMSIS 风格接口：
 *      __get_IPSR()
 *      __get_PRIMASK()
 *      __disable_irq()
 *      __enable_irq()
 * 3. 如果你的工程已经有自己的封装，也可以继续在这里覆写这几个宏。
 * 4. 这样一来，osal_irq.c 仍然只依赖本头文件，用户只是在这里做宏替换。
 */
#if !defined(OSAL_PLATFORM_IRQ_GET_IPSR) || !defined(OSAL_PLATFORM_IRQ_GET_PRIMASK) || \
    !defined(OSAL_PLATFORM_IRQ_RAW_DISABLE) || !defined(OSAL_PLATFORM_IRQ_RAW_ENABLE)
    #if defined(__CC_ARM) && !defined(__clang__)
        #include "cmsis_armcc.h"
    #elif defined(__clang__)
        #include "cmsis_armclang.h"
    #elif defined(__GNUC__)
        #include "cmsis_gcc.h"
    #endif
#endif

#ifndef OSAL_PLATFORM_IRQ_GET_IPSR
#define OSAL_PLATFORM_IRQ_GET_IPSR() __get_IPSR()
#endif

#ifndef OSAL_PLATFORM_IRQ_GET_PRIMASK
#define OSAL_PLATFORM_IRQ_GET_PRIMASK() __get_PRIMASK()
#endif

#ifndef OSAL_PLATFORM_IRQ_RAW_DISABLE
#define OSAL_PLATFORM_IRQ_RAW_DISABLE() __disable_irq()
#endif

#ifndef OSAL_PLATFORM_IRQ_RAW_ENABLE
#define OSAL_PLATFORM_IRQ_RAW_ENABLE() __enable_irq()
#endif

static inline uint32_t osal_platform_arch_irq_disable_save(void) {
    uint32_t primask = OSAL_PLATFORM_IRQ_GET_PRIMASK();
    OSAL_PLATFORM_IRQ_RAW_DISABLE();
    return primask;
}

static inline void osal_platform_arch_irq_restore(uint32_t prev_state) {
    if (prev_state == 0U) {
        OSAL_PLATFORM_IRQ_RAW_ENABLE();
    }
}

#ifndef OSAL_PLATFORM_IRQ_IS_IN_ISR
#define OSAL_PLATFORM_IRQ_IS_IN_ISR() ((OSAL_PLATFORM_IRQ_GET_IPSR() != 0U) ? true : false)
#endif

#ifndef OSAL_PLATFORM_IRQ_DISABLE
#define OSAL_PLATFORM_IRQ_DISABLE() osal_platform_arch_irq_disable_save()
#endif

#ifndef OSAL_PLATFORM_IRQ_ENABLE
#define OSAL_PLATFORM_IRQ_ENABLE() OSAL_PLATFORM_IRQ_RAW_ENABLE()
#endif

#ifndef OSAL_PLATFORM_IRQ_RESTORE
#define OSAL_PLATFORM_IRQ_RESTORE(prev_state) osal_platform_arch_irq_restore((prev_state))
#endif

typedef struct {
    uint32_t (*get_counter_clock_hz)(void);
    uint32_t (*get_reload_value)(void);
    uint32_t (*get_current_value)(void);
    bool (*is_enabled)(void);
    bool (*has_elapsed)(void);
} osal_tick_source_t;

/**
 * @brief 平台初始化钩子。
 * @note 板级适配层可在这里补自己的外设桥接初始化；默认可为空。
 */
void osal_platform_init(void);

/**
 * @brief 自动配置中断分组与 SysTick 优先级。
 * @note 默认实现使用 Cortex-M 原始寄存器；若相关开关为 0，则该步骤会被跳过。
 */
void osal_platform_setup_interrupt_controller(void);

/**
 * @brief 自动配置系统时基。
 * @note 默认实现会按上面的宏配置 Cortex-M 的 SysTick。
 */
void osal_platform_setup_system_tick(void);

/**
 * @brief 获取 OSAL 内部使用的原始 Tick 计数源。
 * @return 返回 system 层内部维护的 Tick 读接口表。
 */
const osal_tick_source_t *osal_platform_get_tick_source(void);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_PLATFORM_H */


