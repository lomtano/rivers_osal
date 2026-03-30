#ifndef OSAL_PLATFORM_STM32F4_H
#define OSAL_PLATFORM_STM32F4_H

#include <stdbool.h>
#include <stdint.h>
#include "main.h"
#include "gpio.h"
#include "usart.h"
#include "stm32f4xx_hal.h"
#include "periph_uart.h"
#include "periph_flash.h"
#include "osal_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ---------------------------------------------------------------------------
 * STM32F4 板级适配层说明
 * ---------------------------------------------------------------------------
 * 1. 本文件是根据 osal_platform_cortexm 模板填写出的具体实例。
 * 2. 当前示例板卡是 STM32F407ZGT6。
 * 3. OSAL 和 STM32 HAL / CMSIS 的直接耦合点，只放在这里和对应的 .c 里。
 * 4. 你移植到别的 STM32 / GD32 / N32 Cortex-M MCU 时，
 *    可以优先复制这两个文件，再替换下面的宏和 Flash 桥接实现。
 */

/**
 * @brief Flash 示例默认使用的内部地址。
 * @note 启用 Flash 示例前，请先改成项目中明确预留的用户扇区。
 */
#ifndef OSAL_PLATFORM_FLASH_DEMO_ADDRESS
#define OSAL_PLATFORM_FLASH_DEMO_ADDRESS 0x080E0000UL
#endif

/**
 * @brief 如需启用 Flash 示例，可打开这个宏。
 */
/* #define OSAL_PLATFORM_ENABLE_FLASH_DEMO */

/*
 * =========================
 * 用户填写区 1：串口桥接宏
 * =========================
 * 这里通常只需要改两件事：
 * 1. 控制台串口句柄是谁
 * 2. 如果想在平台层顺带初始化串口，该调用哪个初始化函数
 */
#ifndef OSAL_PLATFORM_UART_HANDLE
#define OSAL_PLATFORM_UART_HANDLE huart1
#endif

#ifndef OSAL_PLATFORM_UART_INIT
#define OSAL_PLATFORM_UART_INIT() MX_USART1_UART_Init()
#endif

/*
 * =========================
 * 用户填写区 2：板级 LED 示例宏
 * =========================
 * integration 里的点灯任务会调用这两个钩子。
 * 如果你的 LED 引脚不同，只需要改这两个宏。
 */
#ifndef OSAL_PLATFORM_LED1_TOGGLE
#define OSAL_PLATFORM_LED1_TOGGLE() HAL_GPIO_TogglePin(GPIOF, GPIO_PIN_6)
#endif

#ifndef OSAL_PLATFORM_LED2_TOGGLE
#define OSAL_PLATFORM_LED2_TOGGLE() HAL_GPIO_TogglePin(GPIOF, GPIO_PIN_7)
#endif

/*
 * =========================
 * 用户填写区 3：SysTick 原始读接口
 * =========================
 * 这里只提供原始数据：
 * - 时钟频率
 * - 重装值
 * - 当前计数值
 * - 是否使能
 * - 是否发生过一次归零事件
 *
 * 复杂逻辑不写在这里：
 * - us 延时换算
 * - ms tick 维护
 * - 计数回绕处理
 * - 软件定时器最近到期优化
 * 这些都在 system/osal_timer.c 内部完成。
 */
#ifndef OSAL_PLATFORM_TICK_SOURCE_CLOCK_HZ
#define OSAL_PLATFORM_TICK_SOURCE_CLOCK_HZ() HAL_RCC_GetHCLKFreq()
#endif

#ifndef OSAL_PLATFORM_TICK_SOURCE_RELOAD_VALUE
#define OSAL_PLATFORM_TICK_SOURCE_RELOAD_VALUE() (SysTick->LOAD)
#endif

#ifndef OSAL_PLATFORM_TICK_SOURCE_CURRENT_VALUE
#define OSAL_PLATFORM_TICK_SOURCE_CURRENT_VALUE() (SysTick->VAL)
#endif

#ifndef OSAL_PLATFORM_TICK_SOURCE_ENABLED
#define OSAL_PLATFORM_TICK_SOURCE_ENABLED() (((SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) != 0U) ? true : false)
#endif

#ifndef OSAL_PLATFORM_TICK_SOURCE_ELAPSED
#define OSAL_PLATFORM_TICK_SOURCE_ELAPSED() (((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) != 0U) ? true : false)
#endif

void osal_platform_init(void);
const osal_tick_source_t *osal_platform_get_tick_source(void);
periph_uart_t *osal_platform_uart_create(void);
periph_flash_t *osal_platform_flash_create(void);
void osal_platform_led1_toggle(void);
void osal_platform_led2_toggle(void);
bool osal_irq_is_in_isr(void);
uint32_t osal_irq_disable(void);
void osal_irq_enable(void);
void osal_irq_restore(uint32_t prev_state);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_PLATFORM_STM32F4_H */
