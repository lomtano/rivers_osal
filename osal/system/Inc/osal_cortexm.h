#ifndef OSAL_CORTEXM_H
#define OSAL_CORTEXM_H

#include "osal_config.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ---------------------------------------------------------------------------
 * OSAL Cortex-M Platform Boundary
 * ---------------------------------------------------------------------------
 * 1. 本头文件属于 system 层，用来承载 OSAL 内核真正依赖的 Cortex-M 外设配置。
 * 2. 这里的职责只覆盖：
 *    - SysTick 时基
 *    - NVIC 优先级分组 / SysTick 优先级
 *    - DWT profiling
 *    - IRQ/CMSIS 宏替换
 * 3. UART、Flash、LED 这类板级外设桥接不放在这里，仍然留给 platform/example。
 */

/*
 * ---------------------------------------------------------------------------
 * SysTick
 * ---------------------------------------------------------------------------
 * 1. 这一段只描述 OSAL 的系统时基。
 * 2. osal_init() 会调用 osal_cortexm_setup_system_tick()，按这里的宏自动配置 SysTick。
 */
#ifndef OSAL_CORTEXM_CPU_CLOCK_HZ
#define OSAL_CORTEXM_CPU_CLOCK_HZ 168000000UL
#endif
/* CPU 主频。DWT 时间换算和 SysTick 默认都使用这项配置。 */

#ifndef OSAL_CORTEXM_SYSTICK_CLOCK_HZ
#define OSAL_CORTEXM_SYSTICK_CLOCK_HZ OSAL_CORTEXM_CPU_CLOCK_HZ
#endif
/* SysTick 输入时钟。默认与 CPU 主频相同；若改成 HCLK/8 等模式，要同步改这里。 */

#ifndef OSAL_CORTEXM_TICK_RATE_HZ
#define OSAL_CORTEXM_TICK_RATE_HZ 1000UL
#endif
/* OSAL 主 Tick 频率。1000 表示 1ms 触发一次 SysTick 中断。 */

#ifndef OSAL_CORTEXM_SYSTICK_USE_CORE_CLOCK
#define OSAL_CORTEXM_SYSTICK_USE_CORE_CLOCK 1U
#endif
/* 1 表示默认把 SysTick 时钟源配置为 HCLK。 */

#ifndef OSAL_CORTEXM_SYSTICK_HANDLER_NAME
#define OSAL_CORTEXM_SYSTICK_HANDLER_NAME SysTick_Handler
#endif
/* 如果芯片或启动文件里 SysTick 中断函数名不同，可在这里覆盖。 */

#ifndef OSAL_CORTEXM_SYSTICK_CTRL_REG
#define OSAL_CORTEXM_SYSTICK_CTRL_REG (*((volatile uint32_t *)0xE000E010UL))
#endif
/* SysTick 控制寄存器：决定时钟源、中断使能、计数器使能，并提供 COUNTFLAG。 */

#ifndef OSAL_CORTEXM_SYSTICK_LOAD_REG
#define OSAL_CORTEXM_SYSTICK_LOAD_REG (*((volatile uint32_t *)0xE000E014UL))
#endif
/* SysTick 重装值寄存器：计数器每次数到 0 后会从这个值重新装载。 */

#ifndef OSAL_CORTEXM_SYSTICK_CURRENT_VALUE_REG
#define OSAL_CORTEXM_SYSTICK_CURRENT_VALUE_REG (*((volatile uint32_t *)0xE000E018UL))
#endif
/* SysTick 当前值寄存器：读取当前倒计数值，写任意值通常可清零当前计数器。 */

#ifndef OSAL_CORTEXM_SYSTICK_CLK_BIT
#define OSAL_CORTEXM_SYSTICK_CLK_BIT (1UL << 2UL)
#endif
/* CTRL bit2：1 表示使用内核时钟 HCLK，0 表示使用外部分频时钟。 */

#ifndef OSAL_CORTEXM_SYSTICK_INT_BIT
#define OSAL_CORTEXM_SYSTICK_INT_BIT (1UL << 1UL)
#endif
/* CTRL bit1：1 表示允许 SysTick 计数到 0 时产生中断。 */

#ifndef OSAL_CORTEXM_SYSTICK_ENABLE_BIT
#define OSAL_CORTEXM_SYSTICK_ENABLE_BIT (1UL << 0UL)
#endif
/* CTRL bit0：1 表示打开 SysTick 计数器本身。 */

#ifndef OSAL_CORTEXM_SYSTICK_COUNTFLAG_BIT
#define OSAL_CORTEXM_SYSTICK_COUNTFLAG_BIT (1UL << 16UL)
#endif
/* CTRL bit16：只读标志，表示自上次读取后至少发生过一次“计数到 0”。 */

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
} osal_cortexm_tick_source_t;

/*
 * ---------------------------------------------------------------------------
 * NVIC Group (4)
 * ---------------------------------------------------------------------------
 * 1. 这一段负责 Cortex-M 的优先级分组和 SysTick 优先级。
 * 2. 默认目标对齐常见的 Group 4：
 *    - 4bit 抢占优先级
 *    - 0bit 子优先级
 * 3. 如果工程想自行配置这些寄存器，可以把相关自动配置开关改成 0。
 */
#ifndef OSAL_CORTEXM_CONFIGURE_PRIORITY_GROUP
#define OSAL_CORTEXM_CONFIGURE_PRIORITY_GROUP 1U
#endif
/* 1 表示 osal_init() 里自动配置优先级分组。 */

#ifndef OSAL_CORTEXM_NVIC_PRIORITY_GROUP_RAW
#define OSAL_CORTEXM_NVIC_PRIORITY_GROUP_RAW 3U
#endif
/* Cortex-M AIRCR.PRIGROUP 的原始值。对 STM32F4，3U 对应常见的 Group 4。 */

#ifndef OSAL_CORTEXM_CONFIGURE_SYSTICK_PRIORITY
#define OSAL_CORTEXM_CONFIGURE_SYSTICK_PRIORITY 1U
#endif
/* 1 表示 osal_init() 里自动把 SysTick 设置到指定优先级。 */

#ifndef OSAL_CORTEXM_NVIC_PRIO_BITS
#define OSAL_CORTEXM_NVIC_PRIO_BITS 4U
#endif
/* 当前内核实际实现了多少位优先级位。STM32F4 常见是 4。 */

#ifndef OSAL_CORTEXM_SYSTICK_PRIORITY_LEVEL
#define OSAL_CORTEXM_SYSTICK_PRIORITY_LEVEL ((1UL << OSAL_CORTEXM_NVIC_PRIO_BITS) - 1UL)
#endif
/* 默认把 SysTick 放到最低优先级。 */

#ifndef OSAL_CORTEXM_SCB_AIRCR_REG
#define OSAL_CORTEXM_SCB_AIRCR_REG (*((volatile uint32_t *)0xE000ED0CUL))
#endif
/* AIRCR：Application Interrupt and Reset Control Register。 */

#ifndef OSAL_CORTEXM_SCB_SHPR3_REG
#define OSAL_CORTEXM_SCB_SHPR3_REG (*((volatile uint32_t *)0xE000ED20UL))
#endif
/* SHPR3：System Handler Priority Register 3，SysTick 的优先级字段就在这里。 */

#ifndef OSAL_CORTEXM_AIRCR_VECTKEY_POS
#define OSAL_CORTEXM_AIRCR_VECTKEY_POS 16U
#endif

#ifndef OSAL_CORTEXM_AIRCR_VECTKEY_MASK
#define OSAL_CORTEXM_AIRCR_VECTKEY_MASK (0xFFFFUL << OSAL_CORTEXM_AIRCR_VECTKEY_POS)
#endif

#ifndef OSAL_CORTEXM_AIRCR_VECTKEY
#define OSAL_CORTEXM_AIRCR_VECTKEY 0x5FAUL
#endif

#ifndef OSAL_CORTEXM_AIRCR_PRIGROUP_POS
#define OSAL_CORTEXM_AIRCR_PRIGROUP_POS 8U
#endif

#ifndef OSAL_CORTEXM_AIRCR_PRIGROUP_MASK
#define OSAL_CORTEXM_AIRCR_PRIGROUP_MASK (0x7UL << OSAL_CORTEXM_AIRCR_PRIGROUP_POS)
#endif

#ifndef OSAL_CORTEXM_SHPR3_SYSTICK_POS
#define OSAL_CORTEXM_SHPR3_SYSTICK_POS 24U
#endif

#ifndef OSAL_CORTEXM_SHPR3_SYSTICK_MASK
#define OSAL_CORTEXM_SHPR3_SYSTICK_MASK (0xFFUL << OSAL_CORTEXM_SHPR3_SYSTICK_POS)
#endif

/*
 * ---------------------------------------------------------------------------
 * DWT
 * ---------------------------------------------------------------------------
 * 1. 这一段负责 DWT CYCCNT 测量后端。
 * 2. 是否在 osal_init() 时真正配置 DWT，只受 OSAL_CFG_ENABLE_IRQ_PROFILE 控制。
 * 3. profiling 只统计 system 层内部显式包裹的临界区，不统计外部直接调用 osal_irq_* 的范围。
 */
#ifndef OSAL_CORTEXM_CRITICAL_PROFILE_PRINT_INTERVAL_MS
#define OSAL_CORTEXM_CRITICAL_PROFILE_PRINT_INTERVAL_MS 1000U
#endif

#ifndef OSAL_CORTEXM_HAS_DWT_CYCCNT
#define OSAL_CORTEXM_HAS_DWT_CYCCNT 1U
#endif
/* 当前 STM32F407 是 Cortex-M4，默认支持 DWT CYCCNT。若移植到 Cortex-M0/M0+，请改成 0U。 */

#ifndef OSAL_CORTEXM_SCB_DEMCR_REG
#define OSAL_CORTEXM_SCB_DEMCR_REG (*((volatile uint32_t *)0xE000EDFCUL))
#endif

#ifndef OSAL_CORTEXM_DWT_CTRL_REG
#define OSAL_CORTEXM_DWT_CTRL_REG (*((volatile uint32_t *)0xE0001000UL))
#endif

#ifndef OSAL_CORTEXM_DWT_CYCCNT_REG
#define OSAL_CORTEXM_DWT_CYCCNT_REG (*((volatile uint32_t *)0xE0001004UL))
#endif

#ifndef OSAL_CORTEXM_SCB_DEMCR_TRCENA_BIT
#define OSAL_CORTEXM_SCB_DEMCR_TRCENA_BIT (1UL << 24U)
#endif

#ifndef OSAL_CORTEXM_DWT_CTRL_CYCCNTENA_BIT
#define OSAL_CORTEXM_DWT_CTRL_CYCCNTENA_BIT (1UL << 0U)
#endif

#ifndef OSAL_CORTEXM_DWT_HAS_LAR
#define OSAL_CORTEXM_DWT_HAS_LAR 0U
#endif
/* 当前 STM32F4 默认不需要 DWT LAR 解锁。若移植到部分 Cortex-M7 实现，可改成 1U。 */

#if OSAL_CORTEXM_DWT_HAS_LAR
    #ifndef OSAL_CORTEXM_DWT_LAR_REG
    #define OSAL_CORTEXM_DWT_LAR_REG (*((volatile uint32_t *)0xE0001FB0UL))
    #endif

    #ifndef OSAL_CORTEXM_DWT_LAR_UNLOCK_VALUE
    #define OSAL_CORTEXM_DWT_LAR_UNLOCK_VALUE 0xC5ACCE55UL
    #endif
#endif

typedef struct {
    bool profiling_enabled;    /* 当前构建是否打开了 OSAL_CFG_ENABLE_IRQ_PROFILE。 */
    bool timing_supported;     /* 当前内核是否真的支持底层测量源，例如 DWT CYCCNT。 */
    uint32_t cpu_clock_hz;     /* 时间换算统一使用的 CPU 频率。 */
    uint32_t sample_count;     /* 已经累计了多少次完整临界区样本。 */
    uint32_t last_cycles;      /* 最近一次完整临界区耗时，单位 cycle。 */
    uint32_t min_cycles;       /* 历史最短完整临界区耗时，单位 cycle。 */
    uint32_t max_cycles;       /* 历史最长完整临界区耗时，单位 cycle。 */
    uint64_t total_cycles;     /* 历史总 cycle 数，用于计算平均值。 */
    uint32_t avg_cycles;       /* 历史平均完整临界区耗时，单位 cycle。 */
    uint32_t last_ns;          /* 最近一次完整临界区耗时，单位 ns。 */
    uint32_t min_ns;           /* 历史最短完整临界区耗时，单位 ns。 */
    uint32_t max_ns;           /* 历史最长完整临界区耗时，单位 ns。 */
    uint32_t avg_ns;           /* 历史平均完整临界区耗时，单位 ns。 */
} osal_cortexm_profile_stats_t;

/**
 * @brief 初始化 DWT profiling 后端。
 * @note 只有在 OSAL_CFG_ENABLE_IRQ_PROFILE != 0 且平台声明支持 DWT 时才会真正启用。
 */
void osal_cortexm_profile_init(void);

/**
 * @brief 判断当前平台是否支持 DWT profiling。
 * @return 支持返回 true，不支持返回 false。
 */
bool osal_cortexm_profile_is_supported(void);

/**
 * @brief 清零当前累计的 profiling 统计结果。
 * @note 只清统计值，不改变“功能是否启用/平台是否支持”的判定。
 */
void osal_cortexm_profile_reset(void);

/**
 * @brief 读取当前累计的 profiling 统计快照。
 * @param stats 输出缓冲区。
 * @return 当 OSAL_CFG_ENABLE_IRQ_PROFILE 打开且当前平台支持测量时返回 true。
 */
bool osal_cortexm_profile_get_stats(osal_cortexm_profile_stats_t *stats);

/**
 * @brief 把 cycle 数按 OSAL_CORTEXM_CPU_CLOCK_HZ 换算成纳秒。
 * @param cycles CPU cycle 数。
 * @return 换算后的时间，单位 ns。
 */
uint32_t osal_cortexm_profile_cycles_to_ns(uint32_t cycles);

/**
 * @brief 把 cycle 数按 OSAL_CORTEXM_CPU_CLOCK_HZ 换算成微秒。
 * @param cycles CPU cycle 数。
 * @return 换算后的时间，单位 us。
 */
uint32_t osal_cortexm_profile_cycles_to_us(uint32_t cycles);

/*
 * ---------------------------------------------------------------------------
 * MPU
 * ---------------------------------------------------------------------------
 * 预留给后续的 Cortex-M MPU 配置。
 * 当前版本先不放任何代码，只保留边界位置，避免以后继续把内核外设配置混排到别处。
 */

/*
 * ---------------------------------------------------------------------------
 * IRQ / CMSIS Macro Mapping
 * ---------------------------------------------------------------------------
 * 1. 这里保留“宏替换”风格，方便你把 CMSIS 或厂商 SDK 的接口挂进来。
 * 2. irq 模块本身只负责把这些宏包装成 OSAL 风格的统一接口。
 */
#if !defined(OSAL_CORTEXM_IRQ_GET_IPSR) || !defined(OSAL_CORTEXM_IRQ_GET_PRIMASK) || \
    !defined(OSAL_CORTEXM_IRQ_RAW_DISABLE) || !defined(OSAL_CORTEXM_IRQ_RAW_ENABLE)
    #if defined(__CC_ARM) && !defined(__clang__)
        #include "cmsis_armcc.h"
    #elif defined(__clang__)
        #include "cmsis_armclang.h"
    #elif defined(__GNUC__)
        #include "cmsis_gcc.h"
    #endif
#endif

#ifndef OSAL_CORTEXM_IRQ_GET_IPSR
#define OSAL_CORTEXM_IRQ_GET_IPSR() __get_IPSR()
#endif

#ifndef OSAL_CORTEXM_IRQ_GET_PRIMASK
#define OSAL_CORTEXM_IRQ_GET_PRIMASK() __get_PRIMASK()
#endif

#ifndef OSAL_CORTEXM_IRQ_RAW_DISABLE
#define OSAL_CORTEXM_IRQ_RAW_DISABLE() __disable_irq()
#endif

#ifndef OSAL_CORTEXM_IRQ_RAW_ENABLE
#define OSAL_CORTEXM_IRQ_RAW_ENABLE() __enable_irq()
#endif

/**
 * @brief 读取当前 PRIMASK 并立即关闭可屏蔽中断。
 * @return 进入临界区前的原始 PRIMASK 快照。
 * @note 这个内联函数是默认平台 IRQ 宏的底层实现。
 */
static inline uint32_t osal_cortexm_arch_irq_disable_save(void) {
    uint32_t primask = OSAL_CORTEXM_IRQ_GET_PRIMASK();
    OSAL_CORTEXM_IRQ_RAW_DISABLE();
    return primask;
}

/**
 * @brief 按之前保存的 PRIMASK 快照恢复中断状态。
 * @param prev_state 进入临界区前保存的 PRIMASK。
 * @note 只有原本是开中断状态时才会重新开中断。
 */
static inline void osal_cortexm_arch_irq_restore(uint32_t prev_state) {
    if (prev_state == 0U) {
        OSAL_CORTEXM_IRQ_RAW_ENABLE();
    }
}

#ifndef OSAL_CORTEXM_IRQ_IS_IN_ISR
#define OSAL_CORTEXM_IRQ_IS_IN_ISR() ((OSAL_CORTEXM_IRQ_GET_IPSR() != 0U) ? true : false)
#endif

#ifndef OSAL_CORTEXM_IRQ_DISABLE
#define OSAL_CORTEXM_IRQ_DISABLE() osal_cortexm_arch_irq_disable_save()
#endif

#ifndef OSAL_CORTEXM_IRQ_ENABLE
#define OSAL_CORTEXM_IRQ_ENABLE() OSAL_CORTEXM_IRQ_RAW_ENABLE()
#endif

#ifndef OSAL_CORTEXM_IRQ_RESTORE
#define OSAL_CORTEXM_IRQ_RESTORE(prev_state) osal_cortexm_arch_irq_restore((prev_state))
#endif

/**
 * @brief 平台初始化钩子。
 * @note 当前默认实现是 system 层里的显式空实现。
 * @note 如果某个工程确实需要板级初始化逻辑，建议在应用初始化阶段显式调用。
 */
void osal_cortexm_init(void);

/**
 * @brief 自动配置中断分组与 SysTick 优先级。
 * @note 默认实现使用 Cortex-M 原始寄存器；若相关开关为 0，则该步骤会被跳过。
 */
void osal_cortexm_setup_interrupt_controller(void);

/**
 * @brief 自动配置系统时基。
 * @note 默认实现会按上面的宏配置 Cortex-M 的 SysTick。
 */
void osal_cortexm_setup_system_tick(void);

/**
 * @brief 获取 OSAL 内部使用的原始 Tick 计数源。
 * @return 返回 system 层内部维护的 Tick 读接口表。
 */
const osal_cortexm_tick_source_t *osal_cortexm_get_tick_source(void);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_CORTEXM_H */
