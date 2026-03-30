#ifndef OSAL_PLATFORM_CORTEXM_H
#define OSAL_PLATFORM_CORTEXM_H

#include <stdbool.h>
#include <stdint.h>
#include "osal_platform.h"
#include "periph_uart.h"
#include "periph_flash.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 说明：
 * 1. 这是 Cortex-M 裸机平台适配模板。
 * 2. 这个模板不应该承载 OSAL 的系统算法，只负责告诉用户：
 *    - 哪些地方要和 MCU SDK 接起来
 *    - 哪些宏和函数需要按目标芯片填写
 * 3. 复杂的 us/ms 时间换算、软件定时器最近到期优化、回绕处理，都在 system 层内部完成。
 */

/*
 * =========================
 * 用户填写区 1：串口桥接
 * =========================
 * 你需要准备：
 * - 一个串口上下文对象，例如 UART 句柄
 * - 一个“发送单字节”的底层 SDK 函数
 *
 * 下方宏只是模板示意，实际项目里请改成你自己的名字。
 */
#ifndef OSAL_PLATFORM_UART_CONTEXT
#define OSAL_PLATFORM_UART_CONTEXT NULL
#endif

/*
 * 示例：
 * #define OSAL_PLATFORM_UART_WRITE_BYTE(ctx, byte) sdk_uart_send_byte((ctx), (byte))
 */
#ifndef OSAL_PLATFORM_UART_WRITE_BYTE
#define OSAL_PLATFORM_UART_WRITE_BYTE(ctx, byte) OSAL_ERROR
#endif

/*
 * =========================
 * 用户填写区 2：板级 LED 示例
 * =========================
 * 这里只给 integration 里的点灯示例用。
 * 如果你没有 LED，可以保持空操作。
 */
#ifndef OSAL_PLATFORM_LED1_TOGGLE
#define OSAL_PLATFORM_LED1_TOGGLE() ((void)0)
#endif

#ifndef OSAL_PLATFORM_LED2_TOGGLE
#define OSAL_PLATFORM_LED2_TOGGLE() ((void)0)
#endif

/*
 * =========================
 * 用户填写区 3：系统 Tick 原始读接口
 * =========================
 * 这里只需要提供原始数据，不做复杂换算：
 * - 时钟频率
 * - 重装值
 * - 当前计数值
 * - 是否使能
 * - 是否发生过一次归零事件
 *
 * 这些值可以来自 SysTick，也可以来自任何一个等价的 Cortex-M 周期计数源。
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
 * =========================
 * 用户填写区 4：中断接口
 * =========================
 * 这里要挂上：
 * - 判断是否在 ISR
 * - 关中断
 * - 开中断
 * - 恢复中断
 */

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

#endif /* OSAL_PLATFORM_CORTEXM_H */
