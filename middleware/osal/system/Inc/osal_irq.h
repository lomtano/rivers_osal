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

#ifndef OSAL_IRQ_H
#define OSAL_IRQ_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enter a critical section by disabling interrupts.
 * @return Previous interrupt state for osal_irq_restore().
 */
uint32_t osal_irq_disable(void);

/**
 * @brief Enable interrupts unconditionally.
 */
void osal_irq_enable(void);

/**
 * @brief Restore a previously saved interrupt state.
 * @param prev_state Value returned by osal_irq_disable().
 */
void osal_irq_restore(uint32_t prev_state);

/**
 * @brief Check whether the current context is an ISR.
 * @return True when executing inside an interrupt context.
 */
bool osal_irq_is_in_isr(void);

#ifdef __cplusplus
}
#endif

#endif // OSAL_IRQ_H
