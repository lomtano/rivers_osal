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
/* CPU 主频。若系统时钟不是 168MHz，移植时优先改这里。 */

#ifndef OSAL_PLATFORM_SYSTICK_CLOCK_HZ
#define OSAL_PLATFORM_SYSTICK_CLOCK_HZ OSAL_PLATFORM_CPU_CLOCK_HZ
#endif
/* SysTick 输入时钟。默认与 CPU 主频相同；若改成 HCLK/8 等模式，要同步改这里。 */

#ifndef OSAL_PLATFORM_TICK_RATE_HZ
#define OSAL_PLATFORM_TICK_RATE_HZ 1000UL
#endif
/* OSAL 主 Tick 频率。1000 表示 1ms 进一次 SysTick 中断。 */

#ifndef OSAL_PLATFORM_SYSTICK_USE_CORE_CLOCK
#define OSAL_PLATFORM_SYSTICK_USE_CORE_CLOCK 1U
#endif
/* 1 表示默认把 SysTick 时钟源配置为 HCLK。 */

#ifndef OSAL_PLATFORM_SYSTICK_HANDLER_NAME
#define OSAL_PLATFORM_SYSTICK_HANDLER_NAME SysTick_Handler
#endif
/* 如果芯片或启动文件里 SysTick 中断函数名不同，可在这里覆盖。 */

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
/* 1 表示 osal_init() 里自动配置优先级分组。 */

#ifndef OSAL_PLATFORM_NVIC_PRIORITY_GROUP_RAW
#define OSAL_PLATFORM_NVIC_PRIORITY_GROUP_RAW 3U
#endif
/* 默认对应常见的 Group 4 配置。 */

#ifndef OSAL_PLATFORM_CONFIGURE_SYSTICK_PRIORITY
#define OSAL_PLATFORM_CONFIGURE_SYSTICK_PRIORITY 1U
#endif
/* 1 表示 osal_init() 里自动把 SysTick 设置到指定优先级。 */

#ifndef OSAL_PLATFORM_NVIC_PRIO_BITS
#define OSAL_PLATFORM_NVIC_PRIO_BITS 4U
#endif
/* 当前内核实际实现了多少位优先级位。STM32F4 常见是 4。 */

#ifndef OSAL_PLATFORM_SYSTICK_PRIORITY_LEVEL
#define OSAL_PLATFORM_SYSTICK_PRIORITY_LEVEL ((1UL << OSAL_PLATFORM_NVIC_PRIO_BITS) - 1UL)
#endif
/* 默认取最低优先级。 */

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
/* AIRCR：Application Interrupt and Reset Control Register，用来配置优先级分组。 */
#define OSAL_PLATFORM_SCB_AIRCR_REG (*((volatile uint32_t *)0xE000ED0CUL))
#endif

#ifndef OSAL_PLATFORM_SCB_SHPR3_REG
/* SHPR3：System Handler Priority Register 3，SysTick 的优先级字段就在这里。 */
#define OSAL_PLATFORM_SCB_SHPR3_REG (*((volatile uint32_t *)0xE000ED20UL))
#endif

#ifndef OSAL_PLATFORM_SYSTICK_CTRL_REG
/* SysTick 控制寄存器：决定时钟源、中断使能、计数器使能，并提供 COUNTFLAG。 */
#define OSAL_PLATFORM_SYSTICK_CTRL_REG (*((volatile uint32_t *)0xE000E010UL))
#endif

#ifndef OSAL_PLATFORM_SYSTICK_LOAD_REG
/* SysTick 重装值寄存器：计数器每次数到 0 后会从这个值重新装载。 */
#define OSAL_PLATFORM_SYSTICK_LOAD_REG (*((volatile uint32_t *)0xE000E014UL))
#endif

#ifndef OSAL_PLATFORM_SYSTICK_CURRENT_VALUE_REG
/* SysTick 当前值寄存器：读取当前倒计数值，写任意值通常可清零当前计数器。 */
#define OSAL_PLATFORM_SYSTICK_CURRENT_VALUE_REG (*((volatile uint32_t *)0xE000E018UL))
#endif

#ifndef OSAL_PLATFORM_SYSTICK_CLK_BIT
/* CTRL bit2：1 表示使用内核时钟 HCLK，0 表示使用外部分频时钟。 */
#define OSAL_PLATFORM_SYSTICK_CLK_BIT (1UL << 2UL)
#endif

#ifndef OSAL_PLATFORM_SYSTICK_INT_BIT
/* CTRL bit1：1 表示允许 SysTick 计数到 0 时产生中断。 */
#define OSAL_PLATFORM_SYSTICK_INT_BIT (1UL << 1UL)
#endif

#ifndef OSAL_PLATFORM_SYSTICK_ENABLE_BIT
/* CTRL bit0：1 表示打开 SysTick 计数器本身。 */
#define OSAL_PLATFORM_SYSTICK_ENABLE_BIT (1UL << 0UL)
#endif

#ifndef OSAL_PLATFORM_SYSTICK_COUNTFLAG_BIT
/* CTRL bit16：只读标志，表示自上次读取后至少发生过一次“计数到 0”。 */
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
/* AIRCR 写保护钥匙的起始位。 */
#define OSAL_PLATFORM_AIRCR_VECTKEY_POS 16U
#endif

#ifndef OSAL_PLATFORM_AIRCR_VECTKEY_MASK
/* AIRCR 中 VECTKEY 字段掩码。 */
#define OSAL_PLATFORM_AIRCR_VECTKEY_MASK (0xFFFFUL << OSAL_PLATFORM_AIRCR_VECTKEY_POS)
#endif

#ifndef OSAL_PLATFORM_AIRCR_VECTKEY
/* Cortex-M 规定的 AIRCR 解锁值，不带这个值写入会被硬件丢弃。 */
#define OSAL_PLATFORM_AIRCR_VECTKEY 0x5FAUL
#endif

#ifndef OSAL_PLATFORM_AIRCR_PRIGROUP_POS
/* AIRCR 中优先级分组字段的起始位。 */
#define OSAL_PLATFORM_AIRCR_PRIGROUP_POS 8U
#endif

#ifndef OSAL_PLATFORM_AIRCR_PRIGROUP_MASK
/* AIRCR 中优先级分组字段掩码。 */
#define OSAL_PLATFORM_AIRCR_PRIGROUP_MASK (0x7UL << OSAL_PLATFORM_AIRCR_PRIGROUP_POS)
#endif

#ifndef OSAL_PLATFORM_SHPR3_SYSTICK_POS
/* SHPR3 中 SysTick 优先级字节的起始位。 */
#define OSAL_PLATFORM_SHPR3_SYSTICK_POS 24U
#endif

#ifndef OSAL_PLATFORM_SHPR3_SYSTICK_MASK
/* SHPR3 中 SysTick 优先级字段掩码。 */
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
    /* 先记住进入临界区前的 PRIMASK，后面 restore 时才能恢复到原状态。 */
    uint32_t primask = OSAL_PLATFORM_IRQ_GET_PRIMASK();
    /* 真正关闭可屏蔽中断。 */
    OSAL_PLATFORM_IRQ_RAW_DISABLE();
    /* 把旧状态返回给调用方保存。 */
    return primask;
}

static inline void osal_platform_arch_irq_restore(uint32_t prev_state) {
    /* 只有进入临界区前原本是“开中断”状态时，这里才重新打开中断。 */
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
    /* 返回计数器时钟频率。对 SysTick 来说，通常是 HCLK 或 HCLK/8。 */
    uint32_t (*get_counter_clock_hz)(void);
    /* 返回重装值寄存器中的原始计数上限。 */
    uint32_t (*get_reload_value)(void);
    /* 返回当前倒计数值。对递减计数器来说，数值越小表示越接近下一次中断。 */
    uint32_t (*get_current_value)(void);
    /* 返回当前 Tick 源是否已被使能。 */
    bool (*is_enabled)(void);
    /* 返回 COUNTFLAG 或等价“本周期已经跨过一次回绕点”的标志。 */
    bool (*has_elapsed)(void);
} osal_tick_source_t;

/**
 * @brief 平台初始化钩子。
 * @note 当前默认实现是 system 层里的显式空实现。
 * @note 如果某个工程确实需要板级初始化逻辑，建议在应用初始化阶段显式调用，
 *       不再依赖 weak 覆写。
 * @note 这里不是给用户手动配置 SysTick 的地方，SysTick 由下面两个 setup 接口自动处理。
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
 * @note 用户通常只需要改宏，不需要自己手写 SysTick 配置函数。
 */
void osal_platform_setup_system_tick(void);

/**
 * @brief 获取 OSAL 内部使用的原始 Tick 计数源。
 * @return 返回 system 层内部维护的 Tick 读接口表。
 * @note timer 子系统只依赖这个抽象，不直接依赖具体板级适配函数。
 */
const osal_tick_source_t *osal_platform_get_tick_source(void);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_PLATFORM_H */





