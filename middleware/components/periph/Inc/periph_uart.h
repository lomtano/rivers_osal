#ifndef PERIPH_UART_H
#define PERIPH_UART_H

#include <stdint.h>
#include <stdio.h>
#include "osal_status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct periph_uart periph_uart_t;

typedef struct {
    osal_status_t (*write_byte)(void *context, uint8_t byte);
} periph_uart_bridge_t;

/**
 * @brief Create a UART component instance from one byte-write bridge.
 * @param bridge Bridge callbacks for the target MCU SDK.
 * @param context User context passed back into the bridge.
 * @return UART component handle, or NULL on allocation failure.
 */
periph_uart_t *periph_uart_create(const periph_uart_bridge_t *bridge, void *context);

/**
 * @brief Destroy one UART component instance.
 * @param uart UART component handle.
 */
void periph_uart_destroy(periph_uart_t *uart);

/**
 * @brief Send one byte through the UART bridge.
 * @param uart UART component handle.
 * @param byte Byte to send.
 * @return OSAL status code.
 */
osal_status_t periph_uart_write_byte(periph_uart_t *uart, uint8_t byte);

/**
 * @brief Send a byte buffer through the UART bridge.
 * @param uart UART component handle.
 * @param data Source buffer.
 * @param length Number of bytes to send.
 * @return OSAL status code.
 */
osal_status_t periph_uart_write(periph_uart_t *uart, const uint8_t *data, uint32_t length);

/**
 * @brief Send a zero-terminated string through the UART bridge.
 * @param uart UART component handle.
 * @param str String to send.
 * @return OSAL status code.
 */
osal_status_t periph_uart_write_string(periph_uart_t *uart, const char *str);

/**
 * @brief Register one UART component as the stdio console backend.
 * @param uart UART component handle.
 * @return OSAL status code.
 */
osal_status_t periph_uart_bind_console(periph_uart_t *uart);

/**
 * @brief Get the currently bound stdio console backend.
 * @return UART component handle, or NULL when no console is registered.
 */
periph_uart_t *periph_uart_get_console(void);

/**
 * @brief Helper for retargeting fputc to the bound UART console.
 * @param ch Character to output.
 * @param f Standard C stream parameter kept for fputc compatibility.
 * @return ch on success, EOF on error.
 */
int periph_uart_fputc(int ch, FILE *f);

#ifdef __cplusplus
}
#endif

#endif // PERIPH_UART_H
