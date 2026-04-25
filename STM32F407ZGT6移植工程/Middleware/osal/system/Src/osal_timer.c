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
#include "../Inc/osal_cortexm.h"
#include "../Inc/osal.h"
#include <stdbool.h>
#include <stdint.h>

/* 仅供 system 层内部把已知临界区边界送进 DWT profiling。 */
#if OSAL_CFG_ENABLE_SW_TIMER
struct osal_timer_entry {
    bool active;                  /* 当前是否处于启动状态。 */
    bool periodic;                /* true 表示周期定时器，false 表示单次定时器。 */
    uint64_t expiry_us;           /* 下一次绝对到期时间，单位 us。 */
    uint32_t period_us;           /* 周期长度或单次超时时长，单位 us。 */
    osal_timer_callback_t cb;     /* 到期后要调用的回调函数。 */
    void *arg;                    /* 回调函数的用户参数。 */
};
#endif

/* 对外常用的 32 位运行时间，允许自然回绕。 */
static volatile uint32_t s_uptime_us32 = 0U;
static volatile uint32_t s_uptime_ms32 = 0U;
/* 内部保留 64 位累计时间，给软件定时器和长时间运行场景使用。 */
static volatile uint64_t s_uptime_us64 = 0U;
/* 把微秒累计换算成毫秒时剩下的“零头”，避免长期累计截断误差。 */
static volatile uint32_t s_ms_remainder_us = 0U;

#if OSAL_CFG_ENABLE_SW_TIMER
static struct osal_timer_entry *s_timers[OSAL_TIMER_MAX];
static bool s_next_expiry_valid = false;
static uint64_t s_next_expiry_us = 0U;
#endif

/* 原始计数源由 platform 提供，timer 只读取它，不直接依赖某家 HAL。 */
static const osal_cortexm_tick_source_t *s_tick_source = NULL;
static bool s_tick_source_ready = false;
static uint32_t s_tick_counter_hz = 0U;
static uint32_t s_tick_reload_value = 0U;
static uint32_t s_tick_period_ticks = 0U;
/* 一个 OSAL tick 对应多少微秒，由硬件时钟和 reload 值换算得出。 */
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

    /* 32 位 us 计数允许自然回绕，方便与常见 MCU tick 比较方式保持一致。 */
    s_uptime_us32 += delta_us;
    /* 64 位版本提供更长时间范围，给软件定时器和长时间运行场景使用。 */
    s_uptime_us64 += (uint64_t)delta_us;
    /*
     * 不能简单地做 s_uptime_ms32 += delta_us / 1000：
     * 如果每次都丢掉不足 1000us 的余数，长期运行会累计误差。
     */
    total_us = s_ms_remainder_us + delta_us;
    s_uptime_ms32 += (total_us / 1000U);
    s_ms_remainder_us = (total_us % 1000U);
}

/* 函数说明：同步当前平台注册的原始 Tick 源配置。 */
static void osal_timer_sync_tick_source(void) {
    const osal_cortexm_tick_source_t *source = osal_cortexm_get_tick_source();
    uint32_t counter_hz;
    uint32_t reload_value;
    uint32_t period_ticks;
    uint32_t period_us;

    s_tick_source_ready = false;
    s_tick_source = NULL;
    s_tick_counter_hz = 0U;
    s_tick_reload_value = 0U;
    s_tick_period_ticks = 0U;
    /* 先回到保底默认值，只有完整同步成功后才切到真实硬件参数。 */
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
    /* SysTick 一个完整周期包含 LOAD+1 个硬件计数节拍。 */
    period_ticks = reload_value + 1U;

    if ((counter_hz == 0U) || (period_ticks == 0U)) {
        if (!s_tick_invalid_reported) {
            osal_timer_report("tick source returned invalid clock/reload value");
            s_tick_invalid_reported = true;
        }
        return;
    }

    /*
     * 这里把“每个 tick 包含多少硬件计数”换算成“每个 tick 约等于多少微秒”。
     * 加上 counter_hz/2 是为了做四舍五入，而不是纯向下截断。
     */
    period_us = (uint32_t)((((uint64_t)period_ticks) * 1000000ULL + ((uint64_t)counter_hz / 2ULL)) /
                           (uint64_t)counter_hz);
    if (period_us == 0U) {
        /* 时钟极高时，四舍五入后理论上可能得到 0，这里强制最小为 1us。 */
        period_us = 1U;
    }

    s_tick_source = source;
    s_tick_counter_hz = counter_hz;
    s_tick_reload_value = reload_value;
    s_tick_period_ticks = period_ticks;
    /* 把“每次 SysTick 中断大约过去多少微秒”缓存下来，供 tick_handler 直接累加。 */
    s_tick_period_us = period_us;
    s_tick_source_ready = true;
    s_tick_missing_reported = false;
    s_tick_invalid_reported = false;
}

/*
 * 读取子节拍偏移的关键点：
 * 1. SysTick 是递减计数器，且可能在读取过程中刚好回卷。
 * 2. 因此这里采用“读 current -> 读 COUNTFLAG -> 再读 current”的方式缩小竞争窗口。
 * 3. 如果 COUNTFLAG 已经置位，说明中间至少发生过一次回卷，需要按“下一轮计数”重新估算 elapsed_ticks。
 */
/* 函数说明：在关中断保护下读取当前子节拍的微秒偏移。 */
static uint32_t osal_timer_get_subtick_us_locked(void) {
    uint32_t current_before;
    uint32_t current_after;
    uint64_t elapsed_ticks;
    bool elapsed_flag;

    if (!s_tick_source_ready || (s_tick_source == NULL)) {
        /* 没有有效 tick 源时，子节拍偏移退化为 0。 */
        return 0U;
    }

    if (!s_tick_source->is_enabled()) {
        /* 硬件计数器没启动时，也无法估算当前子节拍偏移。 */
        return 0U;
    }

    current_before = s_tick_source->get_current_value();
    /* 读 COUNTFLAG，用来判断这次采样窗口里有没有刚好跨过一次回卷。 */
    elapsed_flag = s_tick_source->has_elapsed();
    current_after = s_tick_source->get_current_value();

    if (elapsed_flag) {
        /* 已经跨过一次回卷，按“完整一个周期 + 新周期已走过的计数”估算。 */
        elapsed_ticks = (uint64_t)s_tick_period_ticks + (uint64_t)s_tick_reload_value - (uint64_t)current_after;
    } else {
        /* 还在当前周期内，直接用 reload - current_before 估算当前已流逝计数。 */
        elapsed_ticks = (uint64_t)s_tick_reload_value - (uint64_t)current_before;
    }

    /* 最终把“已经走过多少硬件时钟节拍”换算成微秒偏移。 */
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
            /* 始终记录所有活动定时器里最早的那个到期时间。 */
            earliest = entry->expiry_us;
            found = true;
        }
    }

    s_next_expiry_valid = found;
    s_next_expiry_us = earliest;
}
#endif

/*
 * 这里需要短暂关中断，把“粗粒度累计时间”和“子节拍偏移”放在同一个一致性窗口里读取。
 * 否则如果读取过程中刚好进入 SysTick 中断，会出现两个来源的时间不匹配。
 */
/* 函数说明：获取 64 位系统运行微秒计数。 */
static uint64_t osal_timer_get_uptime_us64(void) {
    uint32_t irq_state;
    uint64_t now_us;
    uint32_t extra_us;

    irq_state = osal_internal_critical_enter();
    /* 先读“已经完整累计过的时间”。 */
    now_us = s_uptime_us64;
    /* 再把当前这个尚未结束周期中的细粒度偏移补进去。 */
    extra_us = osal_timer_get_subtick_us_locked();
    osal_internal_critical_exit(irq_state);
    /* “粗粒度整 tick 时间 + 当前 tick 内偏移” 才是完整当前时间。 */
    return now_us + (uint64_t)extra_us;
}

/* 函数说明：初始化 OSAL 系统层和平台时基桥接。 */
void osal_init(void) {
    /* 板级适配层可在这里初始化自己的桥接对象，例如串口、Flash、LED。 */
    osal_cortexm_init();
    /* 配置中断分组和 SysTick 优先级。 */
    osal_cortexm_setup_interrupt_controller();
    /* 如果打开了 OSAL_CFG_ENABLE_IRQ_PROFILE，这里会准备临界区测时后端。 */
    osal_cortexm_profile_init();
    /* 启动并配置 SysTick。 */
    osal_cortexm_setup_system_tick();
    /* 把当前硬件 Tick 源参数同步到 timer 子系统。 */
    osal_timer_sync_tick_source();
}

/* 函数说明：在周期性系统 Tick 中断中推进 OSAL 时间基准。 */
void osal_tick_handler(void) {
    if (!s_tick_source_ready) {
        /* 第一次进中断或平台重新配置后，这里会重新同步硬件 tick 参数。 */
        osal_timer_sync_tick_source();
    }

    /* 中断里只做最小工作：把这一个 tick 对应的时间加到账本里。 */
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

    irq_state = osal_internal_critical_enter();
    now_us = s_uptime_us32;
    extra_us = osal_timer_get_subtick_us_locked();
    osal_internal_critical_exit(irq_state);
    /* 32 位接口同样返回“整 tick 累计值 + 子节拍偏移”。 */
    return now_us + extra_us;
}

/* 函数说明：获取 32 位系统运行毫秒计数。 */
uint32_t osal_timer_get_uptime_ms(void) {
    uint32_t irq_state;
    uint32_t now_ms;

    irq_state = osal_internal_critical_enter();
    now_ms = s_uptime_ms32;
    osal_internal_critical_exit(irq_state);
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
        /* 忙等延时故意保持空循环，不让出调度。 */
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
        /* 这里同样是忙等，不是任务级 sleep。 */
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

    irq_state = osal_internal_critical_enter();
    for (i = 0; i < OSAL_TIMER_MAX; ++i) {
        if (s_timers[i] == NULL) {
            /* 找到空槽位后，才为它分配一块真正的控制块。 */
            struct osal_timer_entry *entry =
                (struct osal_timer_entry *)osal_mem_alloc((uint32_t)sizeof(struct osal_timer_entry));

            if (entry == NULL) {
                osal_internal_critical_exit(irq_state);
                return -1;
            }

            entry->active = false;
            entry->periodic = periodic;
            entry->expiry_us = 0U;
            entry->period_us = timeout_us;
            entry->cb = cb;
            entry->arg = arg;
            /* 控制块放进数组后，timer_id 就是这个槽位下标。 */
            s_timers[i] = entry;
            osal_internal_critical_exit(irq_state);
            return i;
        }
    }

    osal_internal_critical_exit(irq_state);
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

    irq_state = osal_internal_critical_enter();
    entry = s_timers[timer_id];
    if ((entry == NULL) || (entry->cb == NULL)) {
        osal_internal_critical_exit(irq_state);
        osal_timer_report("start called on inactive timer id");
        return false;
    }

    /* 启动时把到期时间记录成绝对时间点，后续 poll 直接拿 now_us 比较即可。 */
    entry->expiry_us = osal_timer_get_uptime_us64() + (uint64_t)entry->period_us;
    /* active 置 true 后，poll 才会把它当成有效定时器处理。 */
    entry->active = true;
    osal_timer_refresh_next_expiry();
    osal_internal_critical_exit(irq_state);
    return true;
}

/* 函数说明：动态修改定时器周期；运行中的定时器会从当前时刻重新开始计算下一次到期。 */
bool osal_timer_set_period(int timer_id, uint32_t period_us) {
    uint32_t irq_state;
    uint64_t now_us;
    struct osal_timer_entry *entry;

    if (osal_irq_is_in_isr()) {
        osal_timer_report("set_period is not allowed in ISR context");
        return false;
    }
    if (!osal_timer_is_valid_id(timer_id)) {
        osal_timer_report("set_period called with invalid timer id");
        return false;
    }

    irq_state = osal_internal_critical_enter();
    entry = s_timers[timer_id];
    if ((entry == NULL) || (entry->cb == NULL)) {
        osal_internal_critical_exit(irq_state);
        osal_timer_report("set_period called on inactive timer id");
        return false;
    }

    entry->period_us = period_us;
    if (entry->active) {
        /*
         * 动态改周期时，不沿用旧 expiry_us。
         * 直接从当前时间重新计算下一次到期点，调用方看到的是“新周期立即生效”。
         */
        now_us = osal_timer_get_uptime_us64();
        entry->expiry_us = now_us + (uint64_t)period_us;
        osal_timer_refresh_next_expiry();
    }
    osal_internal_critical_exit(irq_state);
    return true;
}

/* 函数说明：动态修改正在运行的定时器剩余计数值。 */
bool osal_timer_set_remaining(int timer_id, uint32_t remaining_us) {
    uint32_t irq_state;
    uint64_t now_us;
    struct osal_timer_entry *entry;

    if (osal_irq_is_in_isr()) {
        osal_timer_report("set_remaining is not allowed in ISR context");
        return false;
    }
    if (!osal_timer_is_valid_id(timer_id)) {
        osal_timer_report("set_remaining called with invalid timer id");
        return false;
    }

    irq_state = osal_internal_critical_enter();
    entry = s_timers[timer_id];
    if ((entry == NULL) || (entry->cb == NULL)) {
        osal_internal_critical_exit(irq_state);
        osal_timer_report("set_remaining called on inactive timer id");
        return false;
    }
    if (!entry->active) {
        osal_internal_critical_exit(irq_state);
        osal_timer_report("set_remaining called on stopped timer");
        return false;
    }

    now_us = osal_timer_get_uptime_us64();
    entry->expiry_us = now_us + (uint64_t)remaining_us;
    osal_timer_refresh_next_expiry();
    osal_internal_critical_exit(irq_state);
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

    irq_state = osal_internal_critical_enter();
    if (s_timers[timer_id] != NULL) {
        s_timers[timer_id]->active = false;
        /* 停止后重新计算“最近到期时间”，避免 poll 还盯着已经停掉的定时器。 */
        osal_timer_refresh_next_expiry();
        osal_internal_critical_exit(irq_state);
        return;
    }
    osal_internal_critical_exit(irq_state);
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

    irq_state = osal_internal_critical_enter();
    if (s_timers[timer_id] != NULL) {
        /* 先释放控制块，再把数组槽位置空。 */
        osal_mem_free(s_timers[timer_id]);
        s_timers[timer_id] = NULL;
        /* 删除后重新计算“最近到期时间”。 */
        osal_timer_refresh_next_expiry();
        osal_internal_critical_exit(irq_state);
        return;
    }
    osal_internal_critical_exit(irq_state);
    osal_timer_report("delete called on inactive timer id");
}
#endif

/* 函数说明：轮询并处理已经到期的软件定时器。 */
void osal_timer_poll(void) {
#if OSAL_CFG_ENABLE_SW_TIMER
    uint64_t now_us;
    bool handled = false;
    int i;

    /* 没有活动定时器时，直接空返回，避免 OSAL 顶层循环每轮都白扫整张表。 */
    if (!s_next_expiry_valid) {
        return;
    }

    now_us = osal_timer_get_uptime_us64();
    /* 还没到最近一次到期点，说明本轮不可能有任何软件定时器触发。 */
    if (now_us < s_next_expiry_us) {
        return;
    }

    for (i = 0; i < OSAL_TIMER_MAX; ++i) {
        struct osal_timer_entry *entry = s_timers[i];

        if ((entry == NULL) || !entry->active || (now_us < entry->expiry_us)) {
            /* 空槽位、未启动定时器、尚未到期定时器都直接跳过。 */
            continue;
        }

        handled = true;
        if (entry->periodic) {
            if (entry->period_us == 0U) {
                /* 0 周期没有实际意义，直接停掉，避免死循环触发。 */
                entry->active = false;
            } else {
                /*
                 * 周期定时器不是简单地“now + period”：
                 * 这里持续把 expiry_us 向后推进，直到落到 now_us 之后。
                 * 这样即使 OSAL 顶层循环偶尔晚了几拍，也不会把周期漂移永久累计进去。
                 */
                do {
                    /* 按固定周期向前推进，而不是直接写成 now+period。 */
                    entry->expiry_us += (uint64_t)entry->period_us;
                } while (now_us >= entry->expiry_us);
            }
        } else {
            /* 单次定时器触发一次后立即停掉。 */
            entry->active = false;
        }

        if (entry->cb != NULL) {
            /* 回调在普通上下文里执行，不在 SysTick 中断里执行。 */
            entry->cb(entry->arg);
        }
    }

    if (handled) {
        uint32_t irq_state = osal_internal_critical_enter();
        /* 本轮处理过到期定时器后，刷新下一次最早到期的目标时间。 */
        osal_timer_refresh_next_expiry();
        osal_internal_critical_exit(irq_state);
    }
#else
    /* 软件定时器模块关闭时，这里保持为空操作。 */
#endif
}







