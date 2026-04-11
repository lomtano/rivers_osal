#include "../Inc/osal_platform.h"
#include "../Inc/osal.h"

/* 函数说明：输出平台模块的调试诊断信息。 */
static void osal_platform_report(const char *message) {
    OSAL_DEBUG_REPORT("platform", message);
}

/* 函数说明：读取当前系统时基源的输入时钟频率。 */
static uint32_t osal_platform_tick_source_get_clock_hz(void) {
    /* timer 模块会用它把“硬件计数节拍”换算成“OSAL 时间单位”。 */
    return OSAL_PLATFORM_SYSTICK_CLOCK_HZ;
}

/* 函数说明：读取当前系统时基源的重装值。 */
static uint32_t osal_platform_tick_source_get_reload_value(void) {
    /* 这里直接返回 SysTick 当前装载寄存器的值，供 timer 模块做周期换算。 */
    return OSAL_PLATFORM_SYSTICK_LOAD_REG;
}

/* 函数说明：读取当前系统时基源的当前计数值。 */
static uint32_t osal_platform_tick_source_get_current_value(void) {
    /* 这里返回的是递减计数器当前值，不是已经流逝的节拍数。 */
    return OSAL_PLATFORM_SYSTICK_CURRENT_VALUE_REG;
}

/* 函数说明：判断当前系统时基源是否已经使能。 */
static bool osal_platform_tick_source_is_enabled(void) {
    /* 只要 ENABLE 位有效，就说明 SysTick 计数器当前处于工作状态。 */
    return ((OSAL_PLATFORM_SYSTICK_CTRL_REG & OSAL_PLATFORM_SYSTICK_ENABLE_BIT) != 0U);
}

/* 函数说明：判断当前系统时基源是否发生过一次计数回卷。 */
static bool osal_platform_tick_source_has_elapsed(void) {
    /* COUNTFLAG 会在一次完整回卷后置位，用来帮助 timer 模块识别竞争窗口。 */
    return ((OSAL_PLATFORM_SYSTICK_CTRL_REG & OSAL_PLATFORM_SYSTICK_COUNTFLAG_BIT) != 0U);
}

/*
 * system 层内部维护的原始 tick source。
 * timer 模块通过这个对象读取 SysTick 的输入时钟、重装值、当前计数值和回卷标志，
 * 从而把“硬件计数器”转换成统一的 OSAL 时间基。
 */
static const osal_tick_source_t s_tick_source = {
    .get_counter_clock_hz = osal_platform_tick_source_get_clock_hz,
    .get_reload_value = osal_platform_tick_source_get_reload_value,
    .get_current_value = osal_platform_tick_source_get_current_value,
    .is_enabled = osal_platform_tick_source_is_enabled,
    .has_elapsed = osal_platform_tick_source_has_elapsed
};

/* 函数说明：自动配置 Cortex-M 的中断优先级分组。 */
static void osal_platform_configure_priority_group(void) {
#if OSAL_PLATFORM_CONFIGURE_PRIORITY_GROUP
    uint32_t aircr_value;

    if (OSAL_PLATFORM_NVIC_PRIORITY_GROUP_RAW > 7U) {
        osal_platform_report("invalid priority group raw value");
        return;
    }

    /*
     * Cortex-M 的 AIRCR 不是普通寄存器，改 PRIGROUP 时必须同时带上固定的 VECTKEY，
     * 否则写入会被硬件忽略。
     */
    /* 先读出 AIRCR 当前值，避免无关位被误改。 */
    aircr_value = OSAL_PLATFORM_SCB_AIRCR_REG;
    /* 清掉旧的 VECTKEY 和旧的优先级分组字段，为重新写入做准备。 */
    aircr_value &= ~(OSAL_PLATFORM_AIRCR_VECTKEY_MASK | OSAL_PLATFORM_AIRCR_PRIGROUP_MASK);
    /* 写回规定的解锁钥匙，告诉内核“这次 AIRCR 写入是合法配置操作”。 */
    aircr_value |= ((uint32_t)OSAL_PLATFORM_AIRCR_VECTKEY << OSAL_PLATFORM_AIRCR_VECTKEY_POS);
    /* 把用户配置的分组值写进 PRIGROUP 字段。 */
    aircr_value |= ((uint32_t)OSAL_PLATFORM_NVIC_PRIORITY_GROUP_RAW << OSAL_PLATFORM_AIRCR_PRIGROUP_POS);
    /* 最后一次性写回 AIRCR，优先级分组才会真正生效。 */
    OSAL_PLATFORM_SCB_AIRCR_REG = aircr_value;
#else
    /* 如果用户关闭了这项自动配置，就保留当前芯片启动代码里的现有分组设置。 */
#endif
}

/* 函数说明：自动把 SysTick 优先级配置到最低档。 */
static void osal_platform_configure_systick_priority(void) {
#if OSAL_PLATFORM_CONFIGURE_SYSTICK_PRIORITY
    uint32_t encoded_priority;
    uint32_t shpr3_value;

    if ((OSAL_PLATFORM_NVIC_PRIO_BITS == 0U) || (OSAL_PLATFORM_NVIC_PRIO_BITS > 8U)) {
        osal_platform_report("invalid NVIC priority bit width");
        return;
    }

    if (OSAL_PLATFORM_SYSTICK_PRIORITY_LEVEL >= (1UL << OSAL_PLATFORM_NVIC_PRIO_BITS)) {
        osal_platform_report("systick priority level is out of range");
        return;
    }

    /*
     * NVIC 的优先级字段只有高位有效。
     * 这里先按“逻辑优先级”计算，再左移到硬件真正识别的高位位置。
     */
    /* 先把“0~15 这样的逻辑优先级”编码成 NVIC 真正使用的 8bit 字段格式。 */
    encoded_priority = ((uint32_t)OSAL_PLATFORM_SYSTICK_PRIORITY_LEVEL) << (8U - OSAL_PLATFORM_NVIC_PRIO_BITS);
    /* 读出 SHPR3 当前值，避免破坏 PendSV 等其他系统异常的优先级字节。 */
    shpr3_value = OSAL_PLATFORM_SCB_SHPR3_REG;
    /* 只清掉 SysTick 对应的那个字节。 */
    shpr3_value &= ~OSAL_PLATFORM_SHPR3_SYSTICK_MASK;
    /* 把新的 SysTick 优先级编码写入 SHPR3 的高 8 位字段。 */
    shpr3_value |= ((encoded_priority & 0xFFUL) << OSAL_PLATFORM_SHPR3_SYSTICK_POS);
    /* 写回 SHPR3，SysTick 优先级配置完成。 */
    OSAL_PLATFORM_SCB_SHPR3_REG = shpr3_value;
#else
    /* 如果关闭自动配置，SysTick 优先级就交给用户自己的启动代码或 HAL 去处理。 */
#endif
}

/* 函数说明：默认平台初始化钩子；若板级适配层提供强定义，会自动覆盖这里。 */
#if defined(__CC_ARM) && !defined(__clang__)
__weak void osal_platform_init(void) {
}
#else
__attribute__((weak)) void osal_platform_init(void) {
}
#endif

/* 函数说明：返回 system 层内部维护的 Tick 计数源对象。 */
const osal_tick_source_t *osal_platform_get_tick_source(void) {
    /* 返回地址而不是拷贝对象，避免每次查询都复制整组函数指针。 */
    return &s_tick_source;
}

/* 函数说明：按 OSAL 配置自动初始化中断分组和 SysTick 优先级。 */
void osal_platform_setup_interrupt_controller(void) {
    /* 这两个动作放在一起，是因为它们都属于“中断控制器侧的静态配置”。 */
    osal_platform_configure_priority_group();
    osal_platform_configure_systick_priority();
}

/* 函数说明：按 OSAL 配置自动初始化 Cortex-M 的 SysTick。 */
void osal_platform_setup_system_tick(void) {
    uint32_t reload_value;
    uint32_t ctrl_value;

    if ((OSAL_PLATFORM_SYSTICK_CLOCK_HZ == 0UL) || (OSAL_PLATFORM_TICK_RATE_HZ == 0UL)) {
        osal_platform_report("invalid systick clock or tick rate configuration");
        return;
    }

    /* SysTick 的装载值 = 时钟频率 / 目标 tick 频率，并受 24 位计数范围限制。 */
    reload_value = (uint32_t)(OSAL_PLATFORM_SYSTICK_CLOCK_HZ / OSAL_PLATFORM_TICK_RATE_HZ);
    if ((reload_value == 0UL) || (reload_value > 0x01000000UL)) {
        osal_platform_report("systick reload value is out of 24-bit range");
        return;
    }

    /* 默认至少打开“SysTick 中断”和“SysTick 计数器”。 */
    ctrl_value = OSAL_PLATFORM_SYSTICK_INT_BIT | OSAL_PLATFORM_SYSTICK_ENABLE_BIT;
    if (OSAL_PLATFORM_SYSTICK_USE_CORE_CLOCK != 0U) {
        /* 如果选择使用内核时钟，则额外置位 CLKSOURCE。 */
        ctrl_value |= OSAL_PLATFORM_SYSTICK_CLK_BIT;
    }

    /* 先关掉 SysTick，防止在改重装值过程中触发不完整配置。 */
    OSAL_PLATFORM_SYSTICK_CTRL_REG = 0UL;
    /* 清空当前计数值，让新的周期从一个确定状态重新开始。 */
    OSAL_PLATFORM_SYSTICK_CURRENT_VALUE_REG = 0UL;
    /* 装载“计数终点值 - 1”，因为 SysTick 实际计数长度是 LOAD+1。 */
    OSAL_PLATFORM_SYSTICK_LOAD_REG = reload_value - 1UL;
    /* 最后一次性写回 CTRL，真正打开时钟源、中断和计数器。 */
    OSAL_PLATFORM_SYSTICK_CTRL_REG = ctrl_value;

    /*
     * 到这里为止，用户通常不需要再手动调用 HAL_SYSTICK_Config、
     * SysTick_CLKSourceConfig 之类的库函数，除非他明确想接管 SysTick 配置。
     */
}





