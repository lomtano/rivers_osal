#ifndef OSAL_PLATFORM_CORTEXM_H
#define OSAL_PLATFORM_CORTEXM_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ---------------------------------------------------------------------------
 * Cortex-M 适配模板说明
 * ---------------------------------------------------------------------------
 * 1. 本文件只是模板，不需要加入工程参与编译。
 * 2. 你移植到新的 Cortex-M MCU 时，通常会参考本模板去填写：
 *    - platform/example/<board>/osal_platform_<board>.h
 *    - platform/example/<board>/osal_platform_<board>.c
 * 3. Tick / SysTick / IRQ 这些 OSAL 核心能力已经收回 system 层。
 * 4. 所以模板里主要告诉你“system/Inc/osal_platform.h 里哪些宏要填，
 *    以及板级适配层需要补哪些桥接函数”。
 */

/*
 * ---------------------------------------------------------------------------
 * 模板一：system/Inc/osal_platform.h 中常见需要改写的宏
 * ---------------------------------------------------------------------------
 * 下面这些配置通常不放在本文件真正生效，而是作为填写参考：
 *
 *   #define OSAL_PLATFORM_CPU_CLOCK_HZ             168000000UL
 *   #define OSAL_PLATFORM_SYSTICK_CLOCK_HZ         OSAL_PLATFORM_CPU_CLOCK_HZ
 *   #define OSAL_PLATFORM_TICK_RATE_HZ             1000UL
 *   #define OSAL_PLATFORM_CONFIGURE_PRIORITY_GROUP 1U
 *   #define OSAL_PLATFORM_NVIC_PRIORITY_GROUP_RAW  3U
 *   #define OSAL_PLATFORM_CONFIGURE_SYSTICK_PRIORITY 1U
 *   #define OSAL_PLATFORM_NVIC_PRIO_BITS           4U
 *   #define OSAL_PLATFORM_SYSTICK_PRIORITY_LEVEL   15U
 *
 * 如果你希望用 CMSIS 风格替换默认中断宏，也可以在工程里定义：
 *
 *   #define OSAL_PLATFORM_IRQ_GET_IPSR()      __get_IPSR()
 *   #define OSAL_PLATFORM_IRQ_GET_PRIMASK()   __get_PRIMASK()
 *   #define OSAL_PLATFORM_IRQ_RAW_DISABLE()   __disable_irq()
 *   #define OSAL_PLATFORM_IRQ_RAW_ENABLE()    __enable_irq()
 *
 * 这样就能保持：
 *   - system 只依赖 osal_platform.h
 *   - 适配层只负责填参数和桥接板级外设
 */

/*
 * ---------------------------------------------------------------------------
 * 模板二：板级头文件建议长什么样
 * ---------------------------------------------------------------------------
 * 你可以照着下面的思路去写自己的 osal_platform_xxx.h：
 *
 *   #ifndef OSAL_PLATFORM_UART_HANDLE
 *   #define OSAL_PLATFORM_UART_HANDLE huart1
 *   #endif
 *
 *   #ifndef OSAL_PLATFORM_UART_INIT
 *   #define OSAL_PLATFORM_UART_INIT() MX_USART1_UART_Init()
 *   #endif
 *
 *   #ifndef OSAL_PLATFORM_LED1_TOGGLE
 *   #define OSAL_PLATFORM_LED1_TOGGLE() HAL_GPIO_TogglePin(GPIOF, GPIO_PIN_6)
 *   #endif
 *
 *   #ifndef OSAL_PLATFORM_LED2_TOGGLE
 *   #define OSAL_PLATFORM_LED2_TOGGLE() HAL_GPIO_TogglePin(GPIOF, GPIO_PIN_7)
 *   #endif
 */

/*
 * ---------------------------------------------------------------------------
 * 模板三：板级 .c 文件通常需要实现哪些函数
 * ---------------------------------------------------------------------------
 * 1. void osal_platform_init(void);
 *    - 如果板级没有额外初始化需求，可以留空。
 *
 * 2. periph_uart_t *osal_platform_uart_create(void);
 *    - 把 MCU SDK 的“发送单字节”函数桥接给 USART 组件。
 *
 * 3. periph_flash_t *osal_platform_flash_create(void);
 *    - 把 MCU SDK 的解锁、擦除、按不同位宽写入桥接给 Flash 组件。
 *
 * 4. void osal_platform_led1_toggle(void);
 *    void osal_platform_led2_toggle(void);
 *    - 提供点灯示例用的板级钩子。
 */

#ifdef __cplusplus
}
#endif

#endif /* OSAL_PLATFORM_CORTEXM_H */

