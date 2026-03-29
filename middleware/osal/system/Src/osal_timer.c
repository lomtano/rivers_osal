/*
 * osal_timer.c
 * Microsecond tick abstraction and software timer facility.
 * - Internal counters can be driven by a 1us ISR tick or a custom provider
 * - Millisecond uptime is maintained separately so it behaves like HAL_GetTick()
 * - Software timers use the nearest-expiry shortcut to avoid full scans when idle
 */

#include "../Inc/osal_timer.h"
#include "../Inc/osal_irq.h"
#include "../Inc/osal_mem.h"
#include <stdbool.h>
#include <stdint.h>

#if defined(__GNUC__)
/* Weak default microsecond source used until a platform provider is installed. */
__attribute__((weak)) uint32_t osal_hw_get_us(void) { return 0U; }
#elif defined(__ICCARM__)
/* Weak default microsecond source used until a platform provider is installed. */
__weak uint32_t osal_hw_get_us(void) { return 0U; }
#elif defined(__CC_ARM)
/* Weak default microsecond source used until a platform provider is installed. */
__weak uint32_t osal_hw_get_us(void) { return 0U; }
#else
/* Default microsecond source used until a platform provider is installed. */
uint32_t osal_hw_get_us(void) { return 0U; }
#endif

struct osal_timer_entry {
    bool active;
    bool periodic;
    uint64_t expiry_us;
    uint64_t period_us;
    osal_timer_callback_t cb;
    void *arg;
};

static uint32_t (*s_us_provider)(void) = osal_hw_get_us;
static volatile uint32_t s_uptime_us32 = 0U;
static volatile uint32_t s_uptime_ms32 = 0U;
static volatile uint64_t s_uptime_us64 = 0U;
static volatile uint32_t s_ms_remainder_us = 0U;
static bool s_provider_sync_ready = false;
static uint32_t s_provider_last_us = 0U;
static struct osal_timer_entry *s_timers[OSAL_TIMER_MAX];
static bool s_next_expiry_valid = false;
static uint64_t s_next_expiry_us = 0U;

/* Accumulate elapsed microseconds into the public 32-bit and private 64-bit clocks. */
static void osal_timer_accumulate_us(uint32_t delta_us) {
    uint32_t total_us;

    s_uptime_us32 += delta_us;
    s_uptime_us64 += (uint64_t)delta_us;
    total_us = s_ms_remainder_us + delta_us;
    s_uptime_ms32 += (total_us / 1000U);
    s_ms_remainder_us = (total_us % 1000U);
}

/* Synchronize internal counters against a custom microsecond provider. */
static void osal_timer_sync_provider(void) {
    uint32_t now_us;
    uint32_t delta_us;
    uint32_t irq_state;

    if (s_us_provider == osal_hw_get_us) {
        return;
    }

    now_us = s_us_provider();
    irq_state = osal_irq_disable();
    if (!s_provider_sync_ready) {
        s_provider_last_us = now_us;
        s_provider_sync_ready = true;
    } else {
        delta_us = now_us - s_provider_last_us;
        s_provider_last_us = now_us;
        osal_timer_accumulate_us(delta_us);
    }
    osal_irq_restore(irq_state);
}

/* Recalculate the nearest active timer expiry to speed up idle polls. */
static void osal_timer_refresh_next_expiry(void) {
    bool found = false;
    uint64_t earliest = 0U;

    for (int i = 0; i < OSAL_TIMER_MAX; ++i) {
        struct osal_timer_entry *entry = s_timers[i];
        if (entry == NULL || !entry->active) {
            continue;
        }
        if (!found || entry->expiry_us < earliest) {
            earliest = entry->expiry_us;
            found = true;
        }
    }

    s_next_expiry_valid = found;
    s_next_expiry_us = earliest;
}

/* Bind a custom microsecond provider, usually backed by hardware. */
void osal_timer_set_us_provider(uint32_t (*get_us)(void)) {
    uint32_t irq_state = osal_irq_disable();

    s_us_provider = (get_us != NULL) ? get_us : osal_hw_get_us;
    s_provider_sync_ready = false;
    s_provider_last_us = 0U;
    osal_irq_restore(irq_state);
}

/* Increment the internal microsecond counter by one 1us tick. */
void osal_timer_inc_tick(void) {
    if (s_us_provider != osal_hw_get_us) {
        return;
    }
    osal_timer_accumulate_us(1U);
}

/* Read the active 32-bit microsecond counter source. */
uint32_t osal_timer_get_uptime_us(void) {
    osal_timer_sync_provider();
    return s_uptime_us32;
}

/* Derive a millisecond tick from the active microsecond source. */
uint32_t osal_timer_get_uptime_ms(void) {
    osal_timer_sync_provider();
    return s_uptime_ms32;
}

/* Return a HAL-style millisecond tick value. */
uint32_t osal_timer_get_tick(void) {
    return osal_timer_get_uptime_ms();
}

/* Busy-wait until the requested microsecond span has elapsed. */
void osal_timer_delay_us(uint32_t us) {
    uint32_t start = osal_timer_get_uptime_us();
    while ((uint32_t)(osal_timer_get_uptime_us() - start) < us) {
    }
}

/* Allocate one software timer slot and backing control block. */
int osal_timer_create(uint32_t timeout_us, bool periodic, osal_timer_callback_t cb, void *arg) {
    uint32_t irq_state = osal_irq_disable();

    for (int i = 0; i < OSAL_TIMER_MAX; ++i) {
        if (s_timers[i] == NULL) {
            struct osal_timer_entry *entry = (struct osal_timer_entry *)osal_mem_alloc((uint32_t)sizeof(struct osal_timer_entry));
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

/* Arm a software timer using the current uptime as the base. */
bool osal_timer_start(int timer_id) {
    uint32_t irq_state;
    struct osal_timer_entry *entry;

    if (timer_id < 0 || timer_id >= OSAL_TIMER_MAX) {
        return false;
    }

    osal_timer_sync_provider();
    irq_state = osal_irq_disable();
    entry = s_timers[timer_id];
    if (entry == NULL || entry->cb == NULL) {
        osal_irq_restore(irq_state);
        return false;
    }

    entry->expiry_us = s_uptime_us64 + entry->period_us;
    entry->active = true;
    osal_timer_refresh_next_expiry();
    osal_irq_restore(irq_state);
    return true;
}

/* Disarm a software timer without freeing it. */
void osal_timer_stop(int timer_id) {
    uint32_t irq_state;

    if (timer_id < 0 || timer_id >= OSAL_TIMER_MAX) {
        return;
    }

    irq_state = osal_irq_disable();
    if (s_timers[timer_id] != NULL) {
        s_timers[timer_id]->active = false;
        osal_timer_refresh_next_expiry();
    }
    osal_irq_restore(irq_state);
}

/* Free one software timer and release its heap allocation. */
void osal_timer_delete(int timer_id) {
    uint32_t irq_state;

    if (timer_id < 0 || timer_id >= OSAL_TIMER_MAX) {
        return;
    }

    irq_state = osal_irq_disable();
    if (s_timers[timer_id] != NULL) {
        osal_mem_free(s_timers[timer_id]);
        s_timers[timer_id] = NULL;
        osal_timer_refresh_next_expiry();
    }
    osal_irq_restore(irq_state);
}

/* Poll all software timers and invoke callbacks for expired entries. */
void osal_timer_poll(void) {
    uint64_t now_us;
    bool handled = false;
    uint32_t irq_state;

    osal_timer_sync_provider();
    irq_state = osal_irq_disable();
    now_us = s_uptime_us64;
    osal_irq_restore(irq_state);

    if (!s_next_expiry_valid || now_us < s_next_expiry_us) {
        return;
    }

    for (int i = 0; i < OSAL_TIMER_MAX; ++i) {
        struct osal_timer_entry *entry = s_timers[i];
        if (entry == NULL || !entry->active || now_us < entry->expiry_us) {
            continue;
        }

        handled = true;
        if (entry->periodic) {
            if (entry->period_us == 0U) {
                entry->active = false;
            } else {
                do {
                    entry->expiry_us += entry->period_us;
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
        osal_timer_refresh_next_expiry();
    }
}
