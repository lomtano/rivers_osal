#ifndef OSAL_PLATFORM_CORTEXM_H
#define OSAL_PLATFORM_CORTEXM_H

#include <stdbool.h>
#include <stdint.h>
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
 * Cortex-M 适配模板说明
 * ---------------------------------------------------------------------------
 * 1. 这个文件是模板，不是某颗具体 MCU 的最终适配实现。
 * 2. 这个模板只负责告诉用户：OSAL 和 MCU SDK 的耦合点在哪里。
 * 3. 复杂逻辑不要写在平台层：
 *    - us/ms 换算
 *    - tick 回绕处理
 *    - 软件定时器最近到期判断
 *    - 协作式调度
 *    这些都在 system 层。
 * 4. 平台层只负责把 OSAL 和 MCU SDK 接起来。
 * 5. 你移植到新的 Cortex-M MCU 时，通常只需要：
 *    - 复制这个模板生成自己的 osal_platform_xxx.h/.c
 *    - 填下面这些宏和桥接函数
 *    - 不要在平台层堆业务逻辑
 */

/*
 * ---------------------------------------------------------------------------
 * 用户填写区 1：串口桥接宏
 * ---------------------------------------------------------------------------
 * 这里只耦合两件事：
 * 1. 控制台串口上下文对象是谁，例如 UART 句柄
 * 2. 发送单字节时，要调用哪个底层 SDK API
 */
#ifndef OSAL_PLATFORM_UART_CONTEXT
#define OSAL_PLATFORM_UART_CONTEXT NULL
#endif

#ifndef OSAL_PLATFORM_UART_WRITE_BYTE
#define OSAL_PLATFORM_UART_WRITE_BYTE(ctx, byte) OSAL_ERR_RESOURCE
#endif

/*
 * ---------------------------------------------------------------------------
 * 用户填写区 2：板级 LED 示例宏
 * ---------------------------------------------------------------------------
 * integration 里的点灯示例会调用这两个钩子。
 * 如果板子没有 LED，可以保持为空操作。
 */
#ifndef OSAL_PLATFORM_LED1_TOGGLE
#define OSAL_PLATFORM_LED1_TOGGLE() ((void)0)
#endif

#ifndef OSAL_PLATFORM_LED2_TOGGLE
#define OSAL_PLATFORM_LED2_TOGGLE() ((void)0)
#endif

/*
 * ---------------------------------------------------------------------------
 * 用户填写区 3：系统时基原始读接口
 * ---------------------------------------------------------------------------
 * 这里只提供原始数据，不做复杂计算。
 * system 层会利用这些原始值完成：
 * - osal_timer_delay_us()
 * - osal_timer_delay_ms()
 * - osal_timer_get_uptime_us()
 * - osal_timer_get_tick()
 * - 软件定时器调度
 *
 * 你需要提供 5 类原始能力：
 * 1. 计数器输入时钟频率
 * 2. 当前周期的重装值
 * 3. 当前计数值
 * 4. 当前计数器是否使能
 * 5. 自上次读取以来是否发生过一次归零事件
 */
#ifndef OSAL_PLATFORM_TICK_SOURCE_CLOCK_HZ
#define OSAL_PLATFORM_TICK_SOURCE_CLOCK_HZ() 0U
#endif

#ifndef OSAL_PLATFORM_TICK_SOURCE_RELOAD_VALUE
#define OSAL_PLATFORM_TICK_SOURCE_RELOAD_VALUE() 0U
#endif

#ifndef OSAL_PLATFORM_TICK_SOURCE_CURRENT_VALUE
#define OSAL_PLATFORM_TICK_SOURCE_CURRENT_VALUE() 0U
#endif

#ifndef OSAL_PLATFORM_TICK_SOURCE_ENABLED
#define OSAL_PLATFORM_TICK_SOURCE_ENABLED() false
#endif

#ifndef OSAL_PLATFORM_TICK_SOURCE_ELAPSED
#define OSAL_PLATFORM_TICK_SOURCE_ELAPSED() false
#endif

/*
 * ---------------------------------------------------------------------------
 * 用户填写区 4：中断接口
 * ---------------------------------------------------------------------------
 * 这部分需要对接具体内核或 SDK：
 * - 是否在中断上下文
 * - 关全局中断
 * - 开全局中断
 * - 按保存值恢复中断
 */
void osal_platform_init(void);
const osal_tick_source_t *osal_platform_get_tick_source(void);
void osal_platform_led1_toggle(void);
void osal_platform_led2_toggle(void);
bool osal_irq_is_in_isr(void);
uint32_t osal_irq_disable(void);
void osal_irq_enable(void);
void osal_irq_restore(uint32_t prev_state);

#if OSAL_CFG_ENABLE_USART
periph_uart_t *osal_platform_uart_create(void);
#endif

#if OSAL_CFG_ENABLE_FLASH
periph_flash_t *osal_platform_flash_create(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* OSAL_PLATFORM_CORTEXM_H */