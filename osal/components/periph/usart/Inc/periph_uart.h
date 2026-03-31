#ifndef PERIPH_UART_H
#define PERIPH_UART_H

#include <stdint.h>
#include <stdio.h>
#include "osal.h"

#if !OSAL_CFG_ENABLE_USART
#error "OSAL USART component is disabled. Enable OSAL_CFG_ENABLE_USART in osal.h."
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct periph_uart periph_uart_t;

typedef struct {
    osal_status_t (*write_byte)(void *context, uint8_t byte);
} periph_uart_bridge_t;

/*
 * USART 组件句柄契约：
 * 1. create() 成功后，句柄所有权归调用方。
 * 2. destroy(NULL) 是安全空操作。
 * 3. destroy() 成功后，句柄立即失效，不能再次 write / bind / destroy。
 * 4. bind_console() 不转移 USART 对象所有权，只是登记一个“当前控制台后端”。
 * 5. debug 打开时，可检测到的重复 destroy、非法句柄、重复绑定、控制台替换，
 *    会通过 OSAL_DEBUG_HOOK 报告。
 *
 * 接口能力矩阵：
 * - create / destroy / bind_console: 任务态
 * - get_console: 任务态 / ISR
 * - write_byte / write / write_string / fputc: 取决于桥接回调能力，默认按任务态使用
 */

/**
 * @brief 基于“发送单字节”桥接回调创建一个 USART 组件实例。
 * @param bridge 目标 MCU SDK 对应的桥接回调表。
 * @param context 回传给桥接回调的用户上下文。
 * @return 成功时返回 USART 组件句柄，失败时返回 NULL。
 */
periph_uart_t *periph_uart_create(const periph_uart_bridge_t *bridge, void *context);

/**
 * @brief 销毁一个 USART 组件实例。
 * @param uart USART 组件句柄。
 */
void periph_uart_destroy(periph_uart_t *uart);

/**
 * @brief 通过桥接函数发送一个字节。
 * @param uart USART 组件句柄。
 * @param byte 要发送的字节。
 * @return OSAL 状态码。
 */
osal_status_t periph_uart_write_byte(periph_uart_t *uart, uint8_t byte);

/**
 * @brief 通过桥接函数发送一段字节缓冲区。
 * @param uart USART 组件句柄。
 * @param data 源数据缓冲区。
 * @param length 要发送的字节数。
 * @return OSAL 状态码。
 */
osal_status_t periph_uart_write(periph_uart_t *uart, const uint8_t *data, uint32_t length);

/**
 * @brief 通过桥接函数发送一个以零结尾的字符串。
 * @param uart USART 组件句柄。
 * @param str 要发送的字符串。
 * @return OSAL 状态码。
 */
osal_status_t periph_uart_write_string(periph_uart_t *uart, const char *str);

/**
 * @brief 将一个 USART 组件注册为标准输出控制台后端。
 * @param uart USART 组件句柄。
 * @return OSAL 状态码。
 */
osal_status_t periph_uart_bind_console(periph_uart_t *uart);

/**
 * @brief 获取当前绑定的标准输出控制台后端。
 * @return 已绑定的 USART 组件句柄，未绑定时返回 NULL。
 */
periph_uart_t *periph_uart_get_console(void);

/**
 * @brief 用于将 fputc 重定向到当前绑定的 USART 控制台。
 * @param ch 要输出的字符。
 * @param f 为兼容 fputc 保留的标准流参数。
 * @return 成功时返回 ch，失败时返回 EOF。
 */
int periph_uart_fputc(int ch, FILE *f);

#ifdef __cplusplus
}
#endif

#endif /* PERIPH_UART_H */