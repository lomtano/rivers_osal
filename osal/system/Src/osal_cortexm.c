#include "../Inc/osal_cortexm.h"
#include "../Inc/osal.h"
/* 函数说明：输出平台模块的调试诊断信息。 */
static void osal_cortexm_report(const char *message) {
    OSAL_DEBUG_REPORT("cortexm", message);
}

/* -------------------------------------------------------------------------- */
/* SysTick                                                                    */
/* -------------------------------------------------------------------------- */

/* 函数说明：读取当前系统时基源的输入时钟频率。 */
static uint32_t osal_cortexm_tick_source_get_clock_hz(void) {
    return OSAL_CORTEXM_SYSTICK_CLOCK_HZ;
}

/* 函数说明：读取当前系统时基源的重装值。 */
static uint32_t osal_cortexm_tick_source_get_reload_value(void) {
    return OSAL_CORTEXM_SYSTICK_LOAD_REG;
}

/* 函数说明：读取当前系统时基源的当前计数值。 */
static uint32_t osal_cortexm_tick_source_get_current_value(void) {
    return OSAL_CORTEXM_SYSTICK_CURRENT_VALUE_REG;
}

/* 函数说明：判断当前系统时基源是否已经使能。 */
static bool osal_cortexm_tick_source_is_enabled(void) {
    return ((OSAL_CORTEXM_SYSTICK_CTRL_REG & OSAL_CORTEXM_SYSTICK_ENABLE_BIT) != 0U);
}

/* 函数说明：判断当前系统时基源是否发生过一次计数回卷。 */
static bool osal_cortexm_tick_source_has_elapsed(void) {
    return ((OSAL_CORTEXM_SYSTICK_CTRL_REG & OSAL_CORTEXM_SYSTICK_COUNTFLAG_BIT) != 0U);
}

/*
 * system 层内部维护的原始 tick source。
 * timer 模块通过这个对象读取 SysTick 的输入时钟、重装值、当前计数值和回卷标志，
 * 从而把“硬件计数器”转换成统一的 OSAL 时间基。
 */
static const osal_cortexm_tick_source_t s_tick_source = {
    .get_counter_clock_hz = osal_cortexm_tick_source_get_clock_hz,
    .get_reload_value = osal_cortexm_tick_source_get_reload_value,
    .get_current_value = osal_cortexm_tick_source_get_current_value,
    .is_enabled = osal_cortexm_tick_source_is_enabled,
    .has_elapsed = osal_cortexm_tick_source_has_elapsed
};

/* 函数说明：返回 system 层内部维护的 Tick 计数源对象。 */
const osal_cortexm_tick_source_t *osal_cortexm_get_tick_source(void) {
    return &s_tick_source;
}

/* 函数说明：按 OSAL 配置自动初始化 Cortex-M 的 SysTick。 */
void osal_cortexm_setup_system_tick(void) {
    uint32_t reload_value;
    uint32_t ctrl_value;

    if ((OSAL_CORTEXM_SYSTICK_CLOCK_HZ == 0UL) || (OSAL_CORTEXM_TICK_RATE_HZ == 0UL)) {
        osal_cortexm_report("invalid systick clock or tick rate configuration");
        return;
    }

    /* SysTick 的装载值 = 时钟频率 / 目标 tick 频率，并受 24 位计数范围限制。 */
    reload_value = (uint32_t)(OSAL_CORTEXM_SYSTICK_CLOCK_HZ / OSAL_CORTEXM_TICK_RATE_HZ);
    if ((reload_value == 0UL) || (reload_value > 0x01000000UL)) {
        osal_cortexm_report("systick reload value is out of 24-bit range");
        return;
    }

    ctrl_value = OSAL_CORTEXM_SYSTICK_INT_BIT | OSAL_CORTEXM_SYSTICK_ENABLE_BIT;
    if (OSAL_CORTEXM_SYSTICK_USE_CORE_CLOCK != 0U) {
        ctrl_value |= OSAL_CORTEXM_SYSTICK_CLK_BIT;
    }

    /* 先关掉 SysTick，防止在改重装值过程中触发不完整配置。 */
    OSAL_CORTEXM_SYSTICK_CTRL_REG = 0UL;
    /* 清空当前计数值，让新的周期从一个确定状态重新开始。 */
    OSAL_CORTEXM_SYSTICK_CURRENT_VALUE_REG = 0UL;
    /* 装载“计数终点值 - 1”，因为 SysTick 实际计数长度是 LOAD+1。 */
    OSAL_CORTEXM_SYSTICK_LOAD_REG = reload_value - 1UL;
    /* 最后一次性写回 CTRL，真正打开时钟源、中断和计数器。 */
    OSAL_CORTEXM_SYSTICK_CTRL_REG = ctrl_value;
}

/* -------------------------------------------------------------------------- */
/* NVIC Group (4)                                                             */
/* -------------------------------------------------------------------------- */

/* 函数说明：自动配置 Cortex-M 的中断优先级分组。 */
static void osal_cortexm_configure_priority_group(void) {
#if OSAL_CORTEXM_CONFIGURE_PRIORITY_GROUP
    uint32_t aircr_value;

    if (OSAL_CORTEXM_NVIC_PRIORITY_GROUP_RAW > 7U) {
        osal_cortexm_report("invalid priority group raw value");
        return;
    }

    /*
     * Cortex-M 的 AIRCR 不是普通寄存器，改 PRIGROUP 时必须同时带上固定的 VECTKEY，
     * 否则写入会被硬件忽略。
     */
    aircr_value = OSAL_CORTEXM_SCB_AIRCR_REG;
    aircr_value &= ~(OSAL_CORTEXM_AIRCR_VECTKEY_MASK | OSAL_CORTEXM_AIRCR_PRIGROUP_MASK);
    aircr_value |= ((uint32_t)OSAL_CORTEXM_AIRCR_VECTKEY << OSAL_CORTEXM_AIRCR_VECTKEY_POS);
    aircr_value |= ((uint32_t)OSAL_CORTEXM_NVIC_PRIORITY_GROUP_RAW << OSAL_CORTEXM_AIRCR_PRIGROUP_POS);
    OSAL_CORTEXM_SCB_AIRCR_REG = aircr_value;
#else
    /* 如果用户关闭了自动配置，就保留芯片启动代码里的现有分组设置。 */
#endif
}

/* 函数说明：自动把 SysTick 优先级配置到目标档位。 */
static void osal_cortexm_configure_systick_priority(void) {
#if OSAL_CORTEXM_CONFIGURE_SYSTICK_PRIORITY
    uint32_t encoded_priority;
    uint32_t shpr3_value;

    if ((OSAL_CORTEXM_NVIC_PRIO_BITS == 0U) || (OSAL_CORTEXM_NVIC_PRIO_BITS > 8U)) {
        osal_cortexm_report("invalid NVIC priority bit width");
        return;
    }

    if (OSAL_CORTEXM_SYSTICK_PRIORITY_LEVEL >= (1UL << OSAL_CORTEXM_NVIC_PRIO_BITS)) {
        osal_cortexm_report("systick priority level is out of range");
        return;
    }

    /* NVIC 优先级字段只有高位有效，因此要先编码再写入 SHPR3。 */
    encoded_priority = ((uint32_t)OSAL_CORTEXM_SYSTICK_PRIORITY_LEVEL) << (8U - OSAL_CORTEXM_NVIC_PRIO_BITS);
    shpr3_value = OSAL_CORTEXM_SCB_SHPR3_REG;
    shpr3_value &= ~OSAL_CORTEXM_SHPR3_SYSTICK_MASK;
    shpr3_value |= ((encoded_priority & 0xFFUL) << OSAL_CORTEXM_SHPR3_SYSTICK_POS);
    OSAL_CORTEXM_SCB_SHPR3_REG = shpr3_value;
#else
    /* 如果关闭自动配置，SysTick 优先级交给用户自己的启动代码或 HAL 去处理。 */
#endif
}

/* 函数说明：按 OSAL 配置自动初始化中断分组和 SysTick 优先级。 */
void osal_cortexm_setup_interrupt_controller(void) {
    osal_cortexm_configure_priority_group();
    osal_cortexm_configure_systick_priority();
}

/* -------------------------------------------------------------------------- */
/* DWT                                                                        */
/* -------------------------------------------------------------------------- */

typedef struct {
    bool supported;
    bool active;
    uint32_t depth;
    uint32_t start_cycles;
    uint32_t sample_count;
    uint32_t last_cycles;
    uint32_t min_cycles;
    uint32_t max_cycles;
    uint64_t total_cycles;
} osal_cortexm_profile_state_t;

static osal_cortexm_profile_state_t s_cortexm_profile = {0};

/* 函数说明：按 CPU 主频把 cycle 数换算成指定时间单位。 */
static uint32_t osal_cortexm_profile_cycles_to_time_internal(uint32_t cycles, uint32_t scale) {
    if (OSAL_CORTEXM_CPU_CLOCK_HZ == 0UL) {
        return 0U;
    }

    return (uint32_t)((((uint64_t)cycles * (uint64_t)scale) +
                       ((uint64_t)OSAL_CORTEXM_CPU_CLOCK_HZ / 2ULL)) /
                      (uint64_t)OSAL_CORTEXM_CPU_CLOCK_HZ);
}

/* 函数说明：在已关中断的前提下清零 profiling 状态。 */
static void osal_cortexm_profile_clear_locked(void) {
    s_cortexm_profile.active = false;
    s_cortexm_profile.depth = 0U;
    s_cortexm_profile.start_cycles = 0U;
    s_cortexm_profile.sample_count = 0U;
    s_cortexm_profile.last_cycles = 0U;
    s_cortexm_profile.min_cycles = 0U;
    s_cortexm_profile.max_cycles = 0U;
    s_cortexm_profile.total_cycles = 0ULL;
}

/* 函数说明：读取 DWT cycle 计数器当前值。 */
#if OSAL_CFG_ENABLE_IRQ_PROFILE
static uint32_t osal_cortexm_profile_read_cycles_raw(void) {
#if OSAL_CORTEXM_HAS_DWT_CYCCNT
    if (s_cortexm_profile.supported) {
        return OSAL_CORTEXM_DWT_CYCCNT_REG;
    }
#endif
    return 0U;
}
#endif

/* 函数说明：初始化 DWT CYCCNT 测量后端。 */
void osal_cortexm_profile_init(void) {
    uint32_t irq_state;
    bool supported = false;

    irq_state = OSAL_CORTEXM_IRQ_DISABLE();

#if OSAL_CFG_ENABLE_IRQ_PROFILE && OSAL_CORTEXM_HAS_DWT_CYCCNT
    /* 先打开 CoreSight trace 总开关，否则 DWT 相关寄存器可能不可用。 */
    OSAL_CORTEXM_SCB_DEMCR_REG |= OSAL_CORTEXM_SCB_DEMCR_TRCENA_BIT;
#if OSAL_CORTEXM_DWT_HAS_LAR
    /* 部分内核实现需要先解锁 LAR，CYCCNT 才能写入/启动。 */
    OSAL_CORTEXM_DWT_LAR_REG = OSAL_CORTEXM_DWT_LAR_UNLOCK_VALUE;
#endif
    /* 从 0 开始统计，避免把启动前的历史 cycle 混进样本。 */
    OSAL_CORTEXM_DWT_CYCCNT_REG = 0U;
    OSAL_CORTEXM_DWT_CTRL_REG |= OSAL_CORTEXM_DWT_CTRL_CYCCNTENA_BIT;
    supported = ((OSAL_CORTEXM_DWT_CTRL_REG & OSAL_CORTEXM_DWT_CTRL_CYCCNTENA_BIT) != 0U);
#endif

    s_cortexm_profile.supported = supported;
    osal_cortexm_profile_clear_locked();
    OSAL_CORTEXM_IRQ_RESTORE(irq_state);
}

/* 函数说明：判断当前平台是否支持 DWT profiling。 */
bool osal_cortexm_profile_is_supported(void) {
    return s_cortexm_profile.supported;
}

/* 函数说明：清零当前累计的 profiling 统计结果。 */
void osal_cortexm_profile_reset(void) {
    uint32_t irq_state;

    irq_state = OSAL_CORTEXM_IRQ_DISABLE();
    osal_cortexm_profile_clear_locked();
#if OSAL_CFG_ENABLE_IRQ_PROFILE && OSAL_CORTEXM_HAS_DWT_CYCCNT
    if (s_cortexm_profile.supported) {
        OSAL_CORTEXM_DWT_CYCCNT_REG = 0U;
    }
#endif
    OSAL_CORTEXM_IRQ_RESTORE(irq_state);
}

/* 函数说明：读取当前累计的 profiling 统计快照。 */
bool osal_cortexm_profile_get_stats(osal_cortexm_profile_stats_t *stats) {
    uint32_t irq_state;
    uint32_t avg_cycles = 0U;

    if (stats == NULL) {
        return false;
    }

    irq_state = OSAL_CORTEXM_IRQ_DISABLE();
    if (s_cortexm_profile.sample_count != 0U) {
        avg_cycles = (uint32_t)(s_cortexm_profile.total_cycles / (uint64_t)s_cortexm_profile.sample_count);
    }

    stats->profiling_enabled = (OSAL_CFG_ENABLE_IRQ_PROFILE != 0U);
    stats->timing_supported = s_cortexm_profile.supported;
    stats->cpu_clock_hz = OSAL_CORTEXM_CPU_CLOCK_HZ;
    stats->sample_count = s_cortexm_profile.sample_count;
    stats->last_cycles = s_cortexm_profile.last_cycles;
    stats->min_cycles = s_cortexm_profile.min_cycles;
    stats->max_cycles = s_cortexm_profile.max_cycles;
    stats->total_cycles = s_cortexm_profile.total_cycles;
    stats->avg_cycles = avg_cycles;
    stats->last_ns = osal_cortexm_profile_cycles_to_ns(s_cortexm_profile.last_cycles);
    stats->min_ns = osal_cortexm_profile_cycles_to_ns(s_cortexm_profile.min_cycles);
    stats->max_ns = osal_cortexm_profile_cycles_to_ns(s_cortexm_profile.max_cycles);
    stats->avg_ns = osal_cortexm_profile_cycles_to_ns(avg_cycles);
    OSAL_CORTEXM_IRQ_RESTORE(irq_state);

    return (stats->profiling_enabled && stats->timing_supported);
}

/* 函数说明：把 cycle 数按 CPU 主频换算成纳秒。 */
uint32_t osal_cortexm_profile_cycles_to_ns(uint32_t cycles) {
    return osal_cortexm_profile_cycles_to_time_internal(cycles, 1000000000UL);
}

/* 函数说明：把 cycle 数按 CPU 主频换算成微秒。 */
uint32_t osal_cortexm_profile_cycles_to_us(uint32_t cycles) {
    return osal_cortexm_profile_cycles_to_time_internal(cycles, 1000000UL);
}

/*
 * 函数说明：进入一个只属于 system 层的可测量临界区。
 * 注意这里的 depth 只跟踪 system 内部显式包裹的临界区，不关心外部代码是否另外调用过 osal_irq_disable()。
 * 因此即便调用方已经提前关中断，system 内部这段临界区仍然会被单独统计。
 */
void osal_cortexm_profile_enter_internal(void) {
#if OSAL_CFG_ENABLE_IRQ_PROFILE
    if (!s_cortexm_profile.supported) {
        return;
    }

    if (s_cortexm_profile.depth == 0U) {
        s_cortexm_profile.start_cycles = osal_cortexm_profile_read_cycles_raw();
        s_cortexm_profile.active = true;
    }
    ++s_cortexm_profile.depth;
#endif
}

/*
 * 函数说明：退出一个只属于 system 层的可测量临界区。
 * 只有最外层 system 临界区退出时，才会真正结算一笔完整样本。
 */
void osal_cortexm_profile_exit_internal(void) {
#if OSAL_CFG_ENABLE_IRQ_PROFILE
    uint32_t elapsed_cycles;

    if ((!s_cortexm_profile.supported) || (s_cortexm_profile.depth == 0U)) {
        return;
    }

    --s_cortexm_profile.depth;
    if (s_cortexm_profile.depth != 0U) {
        return;
    }
    if (!s_cortexm_profile.active) {
        return;
    }

    elapsed_cycles = osal_cortexm_profile_read_cycles_raw() - s_cortexm_profile.start_cycles;
    s_cortexm_profile.active = false;
    s_cortexm_profile.last_cycles = elapsed_cycles;
    s_cortexm_profile.total_cycles += (uint64_t)elapsed_cycles;
    s_cortexm_profile.sample_count++;

    if (s_cortexm_profile.sample_count == 1U) {
        s_cortexm_profile.min_cycles = elapsed_cycles;
        s_cortexm_profile.max_cycles = elapsed_cycles;
    } else {
        if (elapsed_cycles < s_cortexm_profile.min_cycles) {
            s_cortexm_profile.min_cycles = elapsed_cycles;
        }
        if (elapsed_cycles > s_cortexm_profile.max_cycles) {
            s_cortexm_profile.max_cycles = elapsed_cycles;
        }
    }
#endif
}

/* -------------------------------------------------------------------------- */
/* MPU                                                                        */
/* -------------------------------------------------------------------------- */

/*
 * MPU 预留区。
 * 当前版本暂不实现任何 MPU 配置代码，只保留这个位置，方便后续继续把 Cortex-M 内核外设配置收敛在 cortexm 层。
 */

/* 函数说明：默认平台初始化钩子。 */
void osal_cortexm_init(void) {
}
