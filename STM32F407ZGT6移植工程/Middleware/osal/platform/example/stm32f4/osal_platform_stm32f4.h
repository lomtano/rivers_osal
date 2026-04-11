#ifndef OSAL_PLATFORM_STM32F4_H
#define OSAL_PLATFORM_STM32F4_H

#include <stdbool.h>
#include <stdint.h>
#include "main.h"
#include "gpio.h"
#include "usart.h"
#include "stm32f4xx_hal.h"
#include "osal.h"

#if OSAL_CFG_ENABLE_USART
#include "periph_uart.h"
#endif

#if OSAL_CFG_ENABLE_FLASH
#include "periph_flash.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ---------------------------------------------------------------------------
 * STM32F4 板级适配层说明
 * ---------------------------------------------------------------------------
 * 1. 本文件是当前 STM32F407ZGT6 工程使用的具体适配文件。
 * 2. 这里主要放和板级外设相关的桥接：
 *    - USART
 *    - 内部 Flash
 *    - LED 示例引脚
 * 3. SysTick、IRQ、Tick 原始读接口这些 OSAL 核心能力，已经回到 system 层。
 * 4. 因此你移植到别的板子时，通常只需要改本文件里的外设桥接宏和 .c 实现。
 */

/**
 * @brief Flash 示例默认使用的内部地址。
 * @note 启用 Flash 示例前，请先改成项目中明确预留的用户扇区。
 */
#ifndef OSAL_PLATFORM_FLASH_DEMO_ADDRESS
#define OSAL_PLATFORM_FLASH_DEMO_ADDRESS 0x080E0000UL
#endif
/* 这里只是示例地址，不代表任何工程都能直接拿来写。 */

/**
 * @brief 如需启用 Flash 示例，可把这个宏改成 1。
 * @note 也可以在包含 osal.h 之前先定义它。
 */
#ifndef OSAL_PLATFORM_ENABLE_FLASH_DEMO
#define OSAL_PLATFORM_ENABLE_FLASH_DEMO 0
#endif
/* 默认关闭，避免示例代码误擦写用户工程里的有效数据。 */

/*
 * =========================
 * 用户填写区 1：串口桥接宏
 * =========================
 * 这里只耦合控制台 UART 句柄和初始化函数。
 * 如果你的板子默认控制台不是 USART1，就在这里改。
 */
#ifndef OSAL_PLATFORM_UART_HANDLE
#define OSAL_PLATFORM_UART_HANDLE huart1
#endif

#ifndef OSAL_PLATFORM_UART_INIT
#define OSAL_PLATFORM_UART_INIT() MX_USART1_UART_Init()
#endif
/* 如果串口已经在 main.c 里初始化过，也可以不主动调用这个宏。 */

/*
 * =========================
 * 用户填写区 2：板级 LED 示例宏
 * =========================
 * integration 里的点灯任务会调用这两个钩子。
 * 它们只服务示例，不影响 OSAL 内核本身。
 */
#ifndef OSAL_PLATFORM_LED1_TOGGLE
#define OSAL_PLATFORM_LED1_TOGGLE() HAL_GPIO_TogglePin(GPIOF, GPIO_PIN_6)
#endif

#ifndef OSAL_PLATFORM_LED2_TOGGLE
#define OSAL_PLATFORM_LED2_TOGGLE() HAL_GPIO_TogglePin(GPIOF, GPIO_PIN_7)
#endif

/* 函数说明：完成当前平台所需的 OSAL 适配初始化。 */
void osal_platform_init(void);
/* 函数说明：翻转平台示例中的第一个 LED。 */
void osal_platform_led1_toggle(void);
/* 函数说明：翻转平台示例中的第二个 LED。 */
void osal_platform_led2_toggle(void);

#if OSAL_CFG_ENABLE_USART
/* 函数说明：创建当前平台默认控制台 USART 桥接对象。
 * 这通常是用户接入串口日志/printf 重定向时最先用到的平台接口。 */
periph_uart_t *osal_platform_uart_create(void);
#endif

#if OSAL_CFG_ENABLE_FLASH
/* 函数说明：创建当前平台默认内部 Flash 桥接对象。
 * 这通常是用户接入内部 Flash 读写示例时使用的平台接口。 */
periph_flash_t *osal_platform_flash_create(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* OSAL_PLATFORM_STM32F4_H */

