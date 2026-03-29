#ifndef OSAL_PLATFORM_STM32F4_H
#define OSAL_PLATFORM_STM32F4_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"
#include "periph_uart.h"
#include "periph_flash.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 示例写入使用的内部 Flash 地址。
 * @note 启用 Flash 示例前，请先改成项目中明确预留的用户扇区。
 */
#ifndef OSAL_PLATFORM_FLASH_DEMO_ADDRESS
#define OSAL_PLATFORM_FLASH_DEMO_ADDRESS 0x080E0000UL
#endif

/**
 * @brief 定义该宏后，启动时会自动运行 Flash 示例任务。
 */
/* #define OSAL_PLATFORM_ENABLE_FLASH_DEMO */

/**
 * @brief 选择 STM32F4 侧用于驱动 OSAL 1us tick 的通用定时器实例。
 */
#ifndef OSAL_PLATFORM_TICK_TIM_INSTANCE
#define OSAL_PLATFORM_TICK_TIM_INSTANCE TIM2
#endif

/**
 * @brief 选择 STM32F4 侧用于驱动 OSAL 1us tick 的中断号。
 */
#ifndef OSAL_PLATFORM_TICK_TIM_IRQn
#define OSAL_PLATFORM_TICK_TIM_IRQn TIM2_IRQn
#endif

/**
 * @brief 打开所选通用定时器的时钟。
 */
#ifndef OSAL_PLATFORM_TICK_TIM_CLK_ENABLE
#define OSAL_PLATFORM_TICK_TIM_CLK_ENABLE() __HAL_RCC_TIM2_CLK_ENABLE()
#endif

/**
 * @brief 指示所选通用定时器挂在哪个 APB 总线上。
 * @note APB1 传 1，APB2 传 2。
 */
#ifndef OSAL_PLATFORM_TICK_TIM_APB_BUS
#define OSAL_PLATFORM_TICK_TIM_APB_BUS 1U
#endif

/**
 * @brief OSAL 1us tick 中断优先级。
 */
#ifndef OSAL_PLATFORM_TICK_IRQ_PREEMPT_PRIO
#define OSAL_PLATFORM_TICK_IRQ_PREEMPT_PRIO 0U
#endif

/**
 * @brief OSAL 1us tick 中断子优先级。
 */
#ifndef OSAL_PLATFORM_TICK_IRQ_SUBPRIO
#define OSAL_PLATFORM_TICK_IRQ_SUBPRIO 0U
#endif

/**
 * @brief STM32F4 平台示例初始化钩子。
 */
void osal_platform_init(void);

/**
 * @brief 启动所选 TIMx，并让其以 1MHz 频率产生更新中断。
 * @note 默认使用头文件中的宏配置，也可以按项目需要改宏后复用这套模板。
 */
void osal_platform_tick_start(void);

/**
 * @brief 处理所选 TIMx 的更新中断。
 * @note 在对应的 `TIMx_IRQHandler()` 中直接调用即可。
 */
void osal_platform_tick_irq_handler(void);

/**
 * @brief 清除所选 TIMx 的更新中断标志。
 */
void osal_platform_tick_ack(void);

/**
 * @brief 给使用 SysTick 的工程准备的轻量钩子。
 * @note 如果你选择让 `SysTick_Handler()` 以 1us 周期运行，只需在中断里调用它。
 */
void osal_platform_systick_handler(void);

/**
 * @brief 创建绑定到 STM32F4 SDK 的 USART 桥接组件。
 * @return USART 组件句柄，失败时返回 NULL。
 */
periph_uart_t *osal_platform_uart_create(void);

/**
 * @brief 创建绑定到 STM32F4 SDK 的内部 Flash 桥接组件。
 * @return Flash 组件句柄，失败时返回 NULL。
 */
periph_flash_t *osal_platform_flash_create(void);

/**
 * @brief 切换 LED1 状态。
 * @note 默认是弱符号空实现，移植时请替换成你的板级 GPIO 操作。
 */
void osal_platform_led1_toggle(void);

/**
 * @brief 切换 LED2 状态。
 * @note 默认是弱符号空实现，移植时请替换成你的板级 GPIO 操作。
 */
void osal_platform_led2_toggle(void);

/**
 * @brief 判断当前是否处于中断上下文。
 * @return 处于异常/中断上下文时返回 true。
 */
bool osal_irq_is_in_isr(void);

/**
 * @brief 关闭全局中断，并返回关闭前的 PRIMASK。
 * @return 关闭前的中断屏蔽状态。
 */
uint32_t osal_irq_disable(void);

/**
 * @brief 显式打开全局中断。
 */
void osal_irq_enable(void);

/**
 * @brief 按 `osal_irq_disable()` 保存的状态恢复全局中断。
 * @param prev_state 之前保存的 PRIMASK。
 */
void osal_irq_restore(uint32_t prev_state);

#ifdef __cplusplus
}
#endif

#endif // OSAL_PLATFORM_STM32F4_H
