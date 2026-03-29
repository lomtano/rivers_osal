#ifndef OSAL_PLATFORM_STM32F4_H
#define OSAL_PLATFORM_STM32F4_H

#include <stdbool.h>
#include <stdint.h>
#include "periph_uart.h"
#include "periph_flash.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reserved flash address used by the demo write example.
 * @note Change this to a dedicated user sector before enabling the flash demo task.
 */
#ifndef OSAL_PLATFORM_FLASH_DEMO_ADDRESS
#define OSAL_PLATFORM_FLASH_DEMO_ADDRESS 0x080E0000UL
#endif

/**
 * @brief Define this macro to run the flash demo task automatically at startup.
 */
/* #define OSAL_PLATFORM_ENABLE_FLASH_DEMO */

/**
 * @brief Platform example init hook for STM32F4.
 */
void osal_platform_init(void);

/**
 * @brief Start TIM2 as a 1 MHz update timer for the OSAL 1us tick.
 */
void osal_platform_tick_start(void);

/**
 * @brief Handle the TIM2 update interrupt used by the OSAL tick.
 * @note Call this from TIM2_IRQHandler().
 */
void osal_platform_tick_irq_handler(void);

/**
 * @brief Acknowledge the STM32F4 tick interrupt.
 */
void osal_platform_tick_ack(void);

/**
 * @brief Create the UART bridge component bound to the STM32F4 SDK.
 * @return UART component handle, or NULL on allocation failure.
 */
periph_uart_t *osal_platform_uart_create(void);

/**
 * @brief Create the flash bridge component bound to the STM32F4 SDK.
 * @return Flash component handle, or NULL on allocation failure.
 */
periph_flash_t *osal_platform_flash_create(void);

/**
 * @brief Toggle LED1 for the STM32F4 demo.
 * @note Replace the weak default with your board GPIO operation.
 */
void osal_platform_led1_toggle(void);

/**
 * @brief Toggle LED2 for the STM32F4 demo.
 * @note Replace the weak default with your board GPIO operation.
 */
void osal_platform_led2_toggle(void);

/**
 * @brief Return whether the current STM32F4 context is ISR context.
 * @return True when running inside an interrupt.
 */
bool osal_irq_is_in_isr(void);

/**
 * @brief Disable interrupts on STM32F4 and return the previous PRIMASK value.
 * @return Previous interrupt mask state.
 */
uint32_t osal_irq_disable(void);

/**
 * @brief Restore STM32F4 interrupt state saved by osal_irq_disable().
 * @param prev_state Previous PRIMASK value.
 */
void osal_irq_restore(uint32_t prev_state);

#ifdef __cplusplus
}
#endif

#endif // OSAL_PLATFORM_STM32F4_H
