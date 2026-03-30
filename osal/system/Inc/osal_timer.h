/******************************************************************************
 * Copyright (C) 2024-2026 rivers. All rights reserved.
 *
 * @author JH
 *
 * @version V1.0 2023-12-03
 *
 * @note 1 tab == 4 spaces!
 *
 *****************************************************************************/

#ifndef OSAL_TIMER_H
#define OSAL_TIMER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef OSAL_TIMER_MAX
#define OSAL_TIMER_MAX 16
#endif

typedef void (*osal_timer_callback_t)(void *arg);

typedef struct osal_timer osal_timer_t; // opaque timer object

/**
 * @brief Read OSAL uptime in microseconds.
 * @return 32-bit wraparound microsecond tick.
 */
uint32_t osal_timer_get_uptime_us(void);

/**
 * @brief Read OSAL uptime in milliseconds.
 * @return 32-bit wraparound millisecond tick derived from the microsecond source.
 */
uint32_t osal_timer_get_uptime_ms(void);

/**
 * @brief Read the HAL-style millisecond system tick.
 * @return 32-bit wraparound millisecond tick.
 */
uint32_t osal_timer_get_tick(void);

/**
 * @brief Busy-wait for the requested microseconds.
 * @param us Delay duration in microseconds.
 */
void osal_timer_delay_us(uint32_t us);

/**
 * @brief Increment the internal OSAL tick by 1 microsecond.
 * @note Call this from a fixed 1us periodic timer ISR.
 */
void osal_timer_inc_tick(void);

// software timer api
/**
 * @brief Create a software timer.
 * @param timeout_us Timer period or one-shot timeout in microseconds.
 * @param periodic True for periodic mode, false for one-shot mode.
 * @param cb Callback invoked when the timer expires.
 * @param arg User argument passed to the callback.
 * @return Timer ID, or -1 on failure.
 */
int osal_timer_create(uint32_t timeout_us, bool periodic, osal_timer_callback_t cb, void *arg);

/**
 * @brief Start a software timer.
 * @param timer_id Timer ID returned by osal_timer_create().
 * @return True on success.
 */
bool osal_timer_start(int timer_id);

/**
 * @brief Stop a software timer.
 * @param timer_id Timer ID.
 */
void osal_timer_stop(int timer_id);

/**
 * @brief Delete a software timer.
 * @param timer_id Timer ID.
 */
void osal_timer_delete(int timer_id);

/**
 * @brief Poll all software timers and fire expired callbacks.
 */
void osal_timer_poll(void);

#ifdef __cplusplus
}
#endif

#endif // OSAL_TIMER_H
