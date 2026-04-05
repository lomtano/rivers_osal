/*
 * osal_timer.c
 * OSAL 定时器子系统。
 * - 周期性 Tick 中断只做时间累加
 * - 硬件细分计数读取与周期换算统一放在 system 层
 * - 软件定时器采用“最近到期时间”优化，未到最近事件前不会全表扫描
 */

#include "../Inc/osal_timer.h"
#include "../Inc/osal_irq.h"
#include "../Inc/osal_mem.h"
#include "../Inc/osal_platform.h"
#include "../Inc/osal.h"
#include <stdbool.h>
#include <stdint.h>

#if OSAL_CFG_ENABLE_SW_TIMER
struct osal_timer_entry {
    bool active;
    bool periodic;
    uint64_t expiry_us;
    uint32_t period_us;
    osal_timer_callback_t cb;
    void *arg;
};
#endif

static volatile uint32_t s_uptime_us32 = 0U;
static volatile uint32_t s_uptime_ms32 = 0U;
static volatile uint64_t s_uptime_us64 = 0U;
static volatile uint32_t s_ms_remainder_us = 0U;

#if OSAL_CFG_ENABLE_SW_TIMER
static struct osal_timer_entry *s_timers[OSAL_TIMER_MAX];
static bool s_next_expiry_valid = false;
static uint64_t s_next_expiry_us = 0U;
#endif

static const osal_tick_source_t *s_tick_source = NULL;
static bool s_tick_source_ready = false;
static uint32_t s_tick_counter_hz = 0U;
static uint32_t s_tick_reload_value = 0U;
static uint32_t s_tick_period_ticks = 0U;
static uint32_t s_tick_period_us = OSAL_TICK_PERIOD_US;
static bool s_tick_missing_reported = false;
static bool s_tick_invalid_reported = false;

/* 函数说明：输出定时器模块调试诊断信息。 */
static void osal_timer_report(const char *message) {
    OSAL_DEBUG_REPORT("timer", message);
}

/* 函数说明：累计系统已经流逝的微秒时间。 */
static void osal_timer_accumulate_us(uint32_t delta_us) {
    uint32_t total_us;

    s_uptime_us32 += delta_us;
    s_uptime_us64 += (uint64_t)delta_us;
    total_us = s_ms_remainder_us + delta_us;
    s_uptime_ms32 += (total_us / 1000U);
    s_ms_remainder_us = (total_us % 1000U);
}

/* 函数说明：同步当前平台注册的原始 Tick 源配置。 */
static void osal_timer_sync_tick_source(void) {
    const osal_tick_source_t *source = osal_platform_get_tick_source();
    uint32_t counter_hz;
    uint32_t reload_value;
    uint32_t period_ticks;
    uint32_t period_us;

    s_tick_source_ready = false;
    s_tick_source = NULL;
    s_tick_counter_hz = 0U;
    s_tick_reload_value = 0U;
    s_tick_period_ticks = 0U;
    s_tick_period_us = OSAL_TICK_PERIOD_US;

    if (source == NULL) {
        if (!s_tick_missing_reported) {
            osal_timer_report("tick source is not ready");
            s_tick_missing_reported = true;
        }
        return;
    }

    if ((source->get_counter_clock_hz == NULL) ||
        (source->get_reload_value == NULL) ||
        (source->get_current_value == NULL) ||
        (source->is_enabled == NULL) ||
        (source->has_elapsed == NULL)) {
        if (!s_tick_invalid_reported) {
            osal_timer_report("tick source configuration is incomplete");
            s_tick_invalid_reported = true;
        }
        return;
    }

    counter_hz = source->get_counter_clock_hz();
    reload_value = source->get_reload_value();
    period_ticks = reload_value + 1U;

    if ((counter_hz == 0U) || (period_ticks == 0U)) {
        if (!s_tick_invalid_reported) {
            osal_timer_report("tick source returned invalid clock/reload value");
            s_tick_invalid_reported = true;
        }
        return;
    }

    period_us = (uint32_t)((((uint64_t)period_ticks) * 1000000ULL + ((uint64_t)counter_hz / 2ULL)) /
                           (uint64_t)counter_hz);
    if (period_us == 0U) {
        period_us = 1U;
    }

    s_tick_source = source;
    s_tick_counter_hz = counter_hz;
    s_tick_reload_value = reload_value;
    s_tick_period_ticks = period_ticks;
    s_tick_period_us = period_us;
    s_tick_source_ready = true;
    s_tick_missing_reported = false;
    s_tick_invalid_reported = false;
}

/* 函数说明：在关中断保护下读取当前子节拍的微秒偏移。 */
static uint32_t osal_timer_get_subtick_us_locked(void) {
    uint32_t current_before;
    uint32_t current_after;
    uint64_t elapsed_ticks;
    bool elapsed_flag;

    if (!s_tick_source_ready || (s_tick_source == NULL)) {
        return 0U;
    }

    if (!s_tick_source->is_enabled()) {
        return 0U;
    }

    current_before = s_tick_source->get_current_value();
    elapsed_flag = s_tick_source->has_elapsed();
    current_after = s_tick_source->get_current_value();

    if (elapsed_flag) {
        elapsed_ticks = (uint64_t)s_tick_period_ticks + (uint64_t)s_tick_reload_value - (uint64_t)current_after;
    } else {
        elapsed_ticks = (uint64_t)s_tick_reload_value - (uint64_t)current_before;
    }

    return (uint32_t)((elapsed_ticks * 1000000ULL) / (uint64_t)s_tick_counter_hz);
}

#if OSAL_CFG_ENABLE_SW_TIMER
/* 函数说明：检查软件定时器编号是否合法。 */
static bool osal_timer_is_valid_id(int timer_id) {
    return ((timer_id >= 0) && (timer_id < OSAL_TIMER_MAX));
}

/* 函数说明：刷新最近一次软件定时器到期时间。 */
static void osal_timer_refresh_next_expiry(void) {
    bool found = false;
    uint64_t earliest = 0U;
    int i;

    for (i = 0; i < OSAL_TIMER_MAX; ++i) {
        struct osal_timer_entry *entry = s_timers[i];

        if ((entry == NULL) || !entry->active) {
            continue;
        }

        if (!found || (entry->expiry_us < earliest)) {
            earliest = entry->expiry_us;
            found = true;
        }
    }

    s_next_expiry_valid = found;
    s_next_expiry_us = earliest;
}
#endif

/* 函数说明：获取 64 位系统运行微秒计数。 */
static uint64_t osal_timer_get_uptime_us64(void) {
    uint32_t irq_state;
    uint64_t now_us;
    uint32_t extra_us;

    irq_state = osal_irq_disable();
    now_us = s_uptime_us64;
    extra_us = osal_timer_get_subtick_us_locked();
    osal_irq_restore(irq_state);
    return now_us + (uint64_t)extra_us;
}

/* 函数说明：初始化 OSAL 系统层和平台时基桥接。 */
void osal_init(void) {
    osal_platform_init();
    osal_platform_setup_interrupt_controller();
    osal_platform_setup_system_tick();
    osal_timer_sync_tick_source();
}

/* 函数说明：在周期性系统 Tick 中断中推进 OSAL 时间基准。 */
void osal_tick_handler(void) {
    if (!s_tick_source_ready) {
        osal_timer_sync_tick_source();
    }

    osal_timer_accumulate_us(s_tick_period_us);
}

/* 函数说明：获取 32 位系统运行微秒计数。 */
uint32_t osal_timer_get_uptime_us(void) {
    uint32_t irq_state;
    uint32_t now_us;
    uint32_t extra_us;

    if (!s_tick_source_ready) {
        osal_timer_sync_tick_source();
    }

    irq_state = osal_irq_disable();
    now_us = s_uptime_us32;
    extra_us = osal_timer_get_subtick_us_locked();
    osal_irq_restore(irq_state);
    return now_us + extra_us;
}

/* 函数说明：获取 32 位系统运行毫秒计数。 */
uint32_t osal_timer_get_uptime_ms(void) {
    uint32_t irq_state;
    uint32_t now_ms;

    irq_state = osal_irq_disable();
    now_ms = s_uptime_ms32;
    osal_irq_restore(irq_state);
    return now_ms;
}

/* 函数说明：获取 32 位毫秒节拍值。 */
uint32_t osal_timer_get_tick(void) {
    return osal_timer_get_uptime_ms();
}

/* 函数说明：执行微秒级忙等待延时。 */
void osal_timer_delay_us(uint32_t us) {
    uint32_t start;

    if (osal_irq_is_in_isr()) {
        osal_timer_report("delay_us called in ISR context");
    }

    start = osal_timer_get_uptime_us();
    while ((uint32_t)(osal_timer_get_uptime_us() - start) < us) {
    }
}

/* 函数说明：执行毫秒级忙等待延时。 */
void osal_timer_delay_ms(uint32_t ms) {
    uint32_t start;

    if (osal_irq_is_in_isr()) {
        osal_timer_report("delay_ms called in ISR context");
    }

    start = osal_timer_get_tick();
    while ((uint32_t)(osal_timer_get_tick() - start) < ms) {
    }
}

#if OSAL_CFG_ENABLE_SW_TIMER
/* 函数说明：创建一个软件定时器对象。 */
int osal_timer_create(uint32_t timeout_us, bool periodic, osal_timer_callback_t cb, void *arg) {
    uint32_t irq_state;
    int i;

    if (osal_irq_is_in_isr()) {
        osal_timer_report("create is not allowed in ISR context");
        return -1;
    }
    if (cb == NULL) {
        osal_timer_report("create called with NULL callback");
        return -1;
    }

    irq_state = osal_irq_disable();
    for (i = 0; i < OSAL_TIMER_MAX; ++i) {
        if (s_timers[i] == NULL) {
            struct osal_timer_entry *entry =
                (struct osal_timer_entry *)osal_mem_alloc((uint32_t)sizeof(struct osal_timer_entry));

            if (entry == NULL) {
                osal_irq_restore(irq_state);
                return -1;
            }

            entry->active = false;
            entry->periodic = periodic;
            entry->expiry_us = 0U;
            entry->period_us = timeout_us;
            entry->cb = cb;
            entry->arg = arg;
            s_timers[i] = entry;
            osal_irq_restore(irq_state);
            return i;
        }
    }

    osal_irq_restore(irq_state);
    return -1;
}

/* 函数说明：启动指定的软件定时器。 */
bool osal_timer_start(int timer_id) {
    uint32_t irq_state;
    struct osal_timer_entry *entry;

    if (osal_irq_is_in_isr()) {
        osal_timer_report("start is not allowed in ISR context");
        return false;
    }
    if (!osal_timer_is_valid_id(timer_id)) {
        osal_timer_report("start called with invalid timer id");
        return false;
    }

    irq_state = osal_irq_disable();
    entry = s_timers[timer_id];
    if ((entry == NULL) || (entry->cb == NULL)) {
        osal_irq_restore(irq_state);
        osal_timer_report("start called on inactive timer id");
        return false;
    }

    entry->expiry_us = osal_timer_get_uptime_us64() + (uint64_t)entry->period_us;
    entry->active = true;
    osal_timer_refresh_next_expiry();
    osal_irq_restore(irq_state);
    return true;
}

/* 函数说明：停止指定的软件定时器。 */
void osal_timer_stop(int timer_id) {
    uint32_t irq_state;

    if (osal_irq_is_in_isr()) {
        osal_timer_report("stop is not allowed in ISR context");
        return;
    }
    if (!osal_timer_is_valid_id(timer_id)) {
        osal_timer_report("stop called with invalid timer id");
        return;
    }

    irq_state = osal_irq_disable();
    if (s_timers[timer_id] != NULL) {
        s_timers[timer_id]->active = false;
        osal_timer_refresh_next_expiry();
        osal_irq_restore(irq_state);
        return;
    }
    osal_irq_restore(irq_state);
    osal_timer_report("stop called on inactive timer id");
}

/* 函数说明：删除指定的软件定时器。 */
void osal_timer_delete(int timer_id) {
    uint32_t irq_state;

    if (osal_irq_is_in_isr()) {
        osal_timer_report("delete is not allowed in ISR context");
        return;
    }
    if (!osal_timer_is_valid_id(timer_id)) {
        osal_timer_report("delete called with invalid timer id");
        return;
    }

    irq_state = osal_irq_disable();
    if (s_timers[timer_id] != NULL) {
        osal_mem_free(s_timers[timer_id]);
        s_timers[timer_id] = NULL;
        osal_timer_refresh_next_expiry();
        osal_irq_restore(irq_state);
        return;
    }
    osal_irq_restore(irq_state);
    osal_timer_report("delete called on inactive timer id");
}
#endif

/* 函数说明：轮询并处理已经到期的软件定时器。 */
void osal_timer_poll(void) {
#if OSAL_CFG_ENABLE_SW_TIMER
    uint64_t now_us;
    bool handled = false;
    int i;

    if (!s_next_expiry_valid) {
        return;
    }

    now_us = osal_timer_get_uptime_us64();
    if (now_us < s_next_expiry_us) {
        return;
    }

    for (i = 0; i < OSAL_TIMER_MAX; ++i) {
        struct osal_timer_entry *entry = s_timers[i];

        if ((entry == NULL) || !entry->active || (now_us < entry->expiry_us)) {
            continue;
        }

        handled = true;
        if (entry->periodic) {
            if (entry->period_us == 0U) {
                entry->active = false;
            } else {
                do {
                    entry->expiry_us += (uint64_t)entry->period_us;
                } while (now_us >= entry->expiry_us);
            }
        } else {
            entry->active = false;
        }

        if (entry->cb != NULL) {
            entry->cb(entry->arg);
        }
    }

    if (handled) {
        uint32_t irq_state = osal_irq_disable();
        osal_timer_refresh_next_expiry();
        osal_irq_restore(irq_state);
    }
#else
    /* 软件定时器模块关闭时，这里保持为空操作。 */
#endif
}



