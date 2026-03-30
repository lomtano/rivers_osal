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

#ifndef OSAL_PLATFORM_H
#define OSAL_PLATFORM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Optional platform-level initialization hook.
 */
void osal_platform_init(void);

/**
 * @brief Start the hardware tick source used by OSAL.
 */
void osal_platform_tick_start(void);

/**
 * @brief Optional platform tick IRQ helper.
 */
void osal_platform_tick_irq_handler(void);

/**
 * @brief Acknowledge the platform tick interrupt.
 */
void osal_platform_tick_ack(void);

#ifdef __cplusplus
}
#endif

#endif // OSAL_PLATFORM_H
