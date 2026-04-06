#include "../Inc/osal_platform.h"
#include "../Inc/osal.h"

/* 函数说明：输出平台模块的调试诊断信息。 */
static void osal_platform_report(const char *message) {
    OSAL_DEBUG_REPORT("platform", message);
}

/* 函数说明：读取当前系统时基源的输入时钟频率。 */
static uint32_t osal_platform_tick_source_get_clock_hz(void) {
    return OSAL_PLATFORM_SYSTICK_CLOCK_HZ;
}

/* 函数说明：读取当前系统时基源的重装值。 */
static uint32_t osal_platform_tick_source_get_reload_value(void) {
    return OSAL_PLATFORM_SYSTICK_LOAD_REG;
}

/* 函数说明：读取当前系统时基源的当前计数值。 */
static uint32_t osal_platform_tick_source_get_current_value(void) {
    return OSAL_PLATFORM_SYSTICK_CURRENT_VALUE_REG;
}

/* 函数说明：判断当前系统时基源是否已经使能。 */
static bool osal_platform_tick_source_is_enabled(void) {
    return ((OSAL_PLATFORM_SYSTICK_CTRL_REG & OSAL_PLATFORM_SYSTICK_ENABLE_BIT) != 0U);
}

/* 函数说明：判断当前系统时基源是否发生过一次计数回卷。 */
static bool osal_platform_tick_source_has_elapsed(void) {OSAL_PLATFORM_SYSTICK_USE_CORE_CLOCK
    return ((OSAL_PLATFORM_SYSTICK_CTRL_REG & OSAL_PLATFORM_SYSTICK_COUNTFLAG_BIT) != 0U);
}

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

    aircr_value = OSAL_PLATFORM_SCB_AIRCR_REG;
    aircr_value &= ~(OSAL_PLATFORM_AIRCR_VECTKEY_MASK | OSAL_PLATFORM_AIRCR_PRIGROUP_MASK);
    aircr_value |= ((uint32_t)OSAL_PLATFORM_AIRCR_VECTKEY << OSAL_PLATFORM_AIRCR_VECTKEY_POS);
    aircr_value |= ((uint32_t)OSAL_PLATFORM_NVIC_PRIORITY_GROUP_RAW << OSAL_PLATFORM_AIRCR_PRIGROUP_POS);
    OSAL_PLATFORM_SCB_AIRCR_REG = aircr_value;
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

    encoded_priority = ((uint32_t)OSAL_PLATFORM_SYSTICK_PRIORITY_LEVEL) << (8U - OSAL_PLATFORM_NVIC_PRIO_BITS);
    shpr3_value = OSAL_PLATFORM_SCB_SHPR3_REG;
    shpr3_value &= ~OSAL_PLATFORM_SHPR3_SYSTICK_MASK;
    shpr3_value |= ((encoded_priority & 0xFFUL) << OSAL_PLATFORM_SHPR3_SYSTICK_POS);
    OSAL_PLATFORM_SCB_SHPR3_REG = shpr3_value;
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
    return &s_tick_source;
}

/* 函数说明：按 OSAL 配置自动初始化中断分组和 SysTick 优先级。 */
void osal_platform_setup_interrupt_controller(void) {
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

    reload_value = (uint32_t)(OSAL_PLATFORM_SYSTICK_CLOCK_HZ / OSAL_PLATFORM_TICK_RATE_HZ);
    if ((reload_value == 0UL) || (reload_value > 0x01000000UL)) {
        osal_platform_report("systick reload value is out of 24-bit range");
        return;
    }

    ctrl_value = OSAL_PLATFORM_SYSTICK_INT_BIT | OSAL_PLATFORM_SYSTICK_ENABLE_BIT;
    if (OSAL_PLATFORM_SYSTICK_USE_CORE_CLOCK != 0U) {
        ctrl_value |= OSAL_PLATFORM_SYSTICK_CLK_BIT;
    }

    OSAL_PLATFORM_SYSTICK_CTRL_REG = 0UL;
    OSAL_PLATFORM_SYSTICK_CURRENT_VALUE_REG = 0UL;
    OSAL_PLATFORM_SYSTICK_LOAD_REG = reload_value - 1UL;
    OSAL_PLATFORM_SYSTICK_CTRL_REG = ctrl_value;
}

