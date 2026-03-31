#include "osal.h"

#if OSAL_CFG_ENABLE_USART

#include "../Inc/periph_uart.h"
#include "osal_mem.h"

struct periph_uart {
    const periph_uart_bridge_t *bridge;
    void *context;
    struct periph_uart *next;
};

static periph_uart_t *s_uart_list = NULL;
static periph_uart_t *s_console_uart = NULL;

static void periph_uart_report(const char *message) {
    OSAL_DEBUG_REPORT("usart", message);
}

static void periph_uart_link(periph_uart_t *uart) {
    uart->next = s_uart_list;
    s_uart_list = uart;
}

static bool periph_uart_contains(periph_uart_t *uart) {
    periph_uart_t *current = s_uart_list;

    while (current != NULL) {
        if (current == uart) {
            return true;
        }
        current = current->next;
    }

    return false;
}

static bool periph_uart_unlink(periph_uart_t *uart) {
    periph_uart_t *prev = NULL;
    periph_uart_t *current = s_uart_list;

    while (current != NULL) {
        if (current == uart) {
            if (prev == NULL) {
                s_uart_list = current->next;
            } else {
                prev->next = current->next;
            }
            current->next = NULL;
            return true;
        }
        prev = current;
        current = current->next;
    }

    return false;
}

static bool periph_uart_validate_handle(const periph_uart_t *uart) {
    if (uart == NULL) {
        return false;
    }
#if OSAL_CFG_ENABLE_DEBUG
    if (!periph_uart_contains((periph_uart_t *)uart)) {
        periph_uart_report("API called with inactive USART handle");
        return false;
    }
#endif
    return true;
}

periph_uart_t *periph_uart_create(const periph_uart_bridge_t *bridge, void *context) {
    periph_uart_t *uart;

    if (osal_irq_is_in_isr()) {
        periph_uart_report("create is not allowed in ISR context");
        return NULL;
    }
    if ((bridge == NULL) || (bridge->write_byte == NULL)) {
        periph_uart_report("create called with invalid bridge");
        return NULL;
    }

    uart = (periph_uart_t *)osal_mem_alloc((uint32_t)sizeof(periph_uart_t));
    if (uart == NULL) {
        return NULL;
    }

    uart->bridge = bridge;
    uart->context = context;
    uart->next = NULL;
    periph_uart_link(uart);
    return uart;
}

void periph_uart_destroy(periph_uart_t *uart) {
    if (uart == NULL) {
        return;
    }
    if (osal_irq_is_in_isr()) {
        periph_uart_report("destroy is not allowed in ISR context");
        return;
    }
    if (!periph_uart_unlink(uart)) {
        periph_uart_report("destroy called with inactive USART handle");
        return;
    }

    if (s_console_uart == uart) {
        s_console_uart = NULL;
        periph_uart_report("destroyed USART was bound as current console backend");
    }
    osal_mem_free(uart);
}

osal_status_t periph_uart_write_byte(periph_uart_t *uart, uint8_t byte) {
    if (!periph_uart_validate_handle(uart)) {
        return OSAL_ERR_PARAM;
    }
    if ((uart->bridge == NULL) || (uart->bridge->write_byte == NULL)) {
        return OSAL_ERR_PARAM;
    }
    return uart->bridge->write_byte(uart->context, byte);
}

osal_status_t periph_uart_write(periph_uart_t *uart, const uint8_t *data, uint32_t length) {
    osal_status_t status;
    uint32_t i;

    if ((!periph_uart_validate_handle(uart)) || (data == NULL)) {
        return OSAL_ERR_PARAM;
    }

    for (i = 0U; i < length; ++i) {
        status = periph_uart_write_byte(uart, data[i]);
        if (status != OSAL_OK) {
            return status;
        }
    }

    return OSAL_OK;
}

osal_status_t periph_uart_write_string(periph_uart_t *uart, const char *str) {
    const char *cursor;

    if ((!periph_uart_validate_handle(uart)) || (str == NULL)) {
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
    if (osal_irq_is_in_isr()) {
        periph_uart_report("bind_console is not allowed in ISR context");
        return OSAL_ERR_ISR;
    }
    if (!periph_uart_validate_handle(uart)) {
        return OSAL_ERR_PARAM;
    }
    if (s_console_uart == uart) {
        periph_uart_report("console backend is already bound to this USART instance");
        return OSAL_OK;
    }
    if ((s_console_uart != NULL) && (s_console_uart != uart)) {
        periph_uart_report("console backend has been replaced by another USART instance");
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

#endif /* OSAL_CFG_ENABLE_USART */