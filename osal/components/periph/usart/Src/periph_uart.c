#include "../Inc/periph_uart.h"
#include "osal_mem.h"

struct periph_uart {
    const periph_uart_bridge_t *bridge;
    void *context;
};

static periph_uart_t *s_console_uart = NULL;

periph_uart_t *periph_uart_create(const periph_uart_bridge_t *bridge, void *context) {
    periph_uart_t *uart;

    if (bridge == NULL || bridge->write_byte == NULL) {
        return NULL;
    }

    uart = (periph_uart_t *)osal_mem_alloc((uint32_t)sizeof(periph_uart_t));
    if (uart == NULL) {
        return NULL;
    }

    uart->bridge = bridge;
    uart->context = context;
    return uart;
}

void periph_uart_destroy(periph_uart_t *uart) {
    if (uart == NULL) {
        return;
    }
    if (s_console_uart == uart) {
        s_console_uart = NULL;
    }
    osal_mem_free(uart);
}

osal_status_t periph_uart_write_byte(periph_uart_t *uart, uint8_t byte) {
    if (uart == NULL || uart->bridge == NULL || uart->bridge->write_byte == NULL) {
        return OSAL_ERR_PARAM;
    }
    return uart->bridge->write_byte(uart->context, byte);
}

osal_status_t periph_uart_write(periph_uart_t *uart, const uint8_t *data, uint32_t length) {
    osal_status_t status;

    if (uart == NULL || data == NULL) {
        return OSAL_ERR_PARAM;
    }

    for (uint32_t i = 0U; i < length; ++i) {
        status = periph_uart_write_byte(uart, data[i]);
        if (status != OSAL_OK) {
            return status;
        }
    }

    return OSAL_OK;
}

osal_status_t periph_uart_write_string(periph_uart_t *uart, const char *str) {
    const char *cursor;

    if (uart == NULL || str == NULL) {
        return OSAL_ERR_PARAM;
    }

    cursor = str;
    while (*cursor != '\0') {
        osal_status_t status = periph_uart_write_byte(uart, (uint8_t)(*cursor));
        if (status != OSAL_OK) {
            return status;
        }
        ++cursor;
    }

    return OSAL_OK;
}

osal_status_t periph_uart_bind_console(periph_uart_t *uart) {
    if (uart == NULL) {
        return OSAL_ERR_PARAM;
    }
    s_console_uart = uart;
    return OSAL_OK;
}

periph_uart_t *periph_uart_get_console(void) {
    return s_console_uart;
}

int periph_uart_fputc(int ch, FILE *f) {
    (void)f;

    if (s_console_uart == NULL) {
        return EOF;
    }

    if (periph_uart_write_byte(s_console_uart, (uint8_t)ch) != OSAL_OK) {
        return EOF;
    }

    return ch;
}
