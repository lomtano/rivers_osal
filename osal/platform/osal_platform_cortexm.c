#include "osal_platform_cortexm.h"

/*
 * ---------------------------------------------------------------------------
 * Cortex-M 适配模板占位文件
 * ---------------------------------------------------------------------------
 * 1. 本文件不参与当前 STM32F407 工程编译。
 * 2. 它存在的目的只是告诉你：真正需要复制出去填写的是
 *    platform/example/<board>/ 下的具体适配文件。
 * 3. Tick / SysTick / IRQ 的核心逻辑已经在 system 层统一实现，
 *    所以本文件只保留“板级外设桥接模板”。
 * 4. 如果你新建了自己的板级适配文件，可以把下面这段思路照着搬过去：
 *
 *   - 在 .h 里填写 UART 句柄、LED 引脚宏、Flash 示例地址
 *   - 在 .c 里实现：
 *       osal_platform_init()
 *       osal_platform_uart_create()
 *       osal_platform_flash_create()
 *       osal_platform_led1_toggle()
 *       osal_platform_led2_toggle()
 *
 * 5. system/Inc/osal_platform.h 里那组主频、SysTick 和 IRQ 宏才是用户要先填写的
 *    核心配置；这里更多是 USART / Flash / LED 这类板级桥接模板。
 */

#if 0
/* 下面仅作示意，不参与编译。 */

void osal_platform_init(void) {
    /* 如果你的板级没有额外初始化需求，可以留空。 */
}

periph_uart_t *osal_platform_uart_create(void) {
    /* 把 MCU SDK 的单字节发送接口桥接到 periph_uart。 */
    return NULL;
}

periph_flash_t *osal_platform_flash_create(void) {
    /* 把 MCU SDK 的 Flash 解锁/擦除/写入接口桥接到 periph_flash。 */
    return NULL;
}

void osal_platform_led1_toggle(void) {
    /* 翻转你的 LED1 引脚。 */
}

void osal_platform_led2_toggle(void) {
    /* 翻转你的 LED2 引脚。 */
}
#endif

