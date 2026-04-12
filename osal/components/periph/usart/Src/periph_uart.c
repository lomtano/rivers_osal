#include "osal.h"

#if OSAL_CFG_ENABLE_USART

#include "../Inc/periph_uart.h"
#include "osal_mem.h"

/*
 * USART 组件对象：
 * 1. bridge 指向“如何发单字节”的桥接函数表。
 * 2. context 保存底层串口句柄或用户上下文。
 * 3. next 用于活动对象链表。
 */
struct periph_uart {
    const periph_uart_bridge_t *bridge;
    void *context;
    struct periph_uart *next;
};

static periph_uart_t *s_uart_list = NULL;
/* 当前绑定到标准输出的 USART 组件实例。 */
static periph_uart_t *s_console_uart = NULL;

/* 函数说明：输出 USART 组件调试诊断信息。 */
static void periph_uart_report(const char *message) {
    OSAL_DEBUG_REPORT("usart", message);
}

/* 函数说明：将 USART 对象挂入活动链表。 */
static void periph_uart_link(periph_uart_t *uart) {
    /* 头插到活动链表，后续 destroy/validate 都靠这张链表工作。 */
    uart->next = s_uart_list;
    s_uart_list = uart;
}

/* 函数说明：检查 USART 句柄是否仍在活动链表中。 */
#if OSAL_CFG_ENABLE_DEBUG
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
#endif

/* 函数说明：将 USART 对象从活动链表中摘除。 */
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

/* 函数说明：校验 USART 句柄是否有效。 */
static bool periph_uart_validate_handle(const periph_uart_t *uart) {
    if (uart == NULL) {
        return false;
    }
#if OSAL_CFG_ENABLE_DEBUG
    if (!periph_uart_contains((periph_uart_t *)uart)) {
        /* debug 模式下才做活动链表校验，release 模式减少一点额外遍历。 */
        periph_uart_report("API called with inactive USART handle");
        return false;
    }
#endif
    return true;
}

/* 函数说明：创建 USART 桥接对象。 */
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

    /* bridge 决定“怎么发一个字节”，context 决定“发给哪个底层串口实例”。 */
    uart->bridge = bridge;
    uart->context = context;
    uart->next = NULL;
    periph_uart_link(uart);
    return uart;
}

/* 函数说明：销毁 USART 桥接对象。 */
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
        /* 被销毁的是当前控制台后端时，要顺手把 console 解绑，避免悬空句柄。 */
        s_console_uart = NULL;
        periph_uart_report("destroyed USART was bound as current console backend");
    }
    /* 控制块本身来自 OSAL 统一堆，最后统一回收。 */
    osal_mem_free(uart);
}

/* 函数说明：通过桥接接口发送一个字节。 */
osal_status_t periph_uart_write_byte(periph_uart_t *uart, uint8_t byte) {
    if (!periph_uart_validate_handle(uart)) {
        return OSAL_ERR_PARAM;
    }
    if ((uart->bridge == NULL) || (uart->bridge->write_byte == NULL)) {
        return OSAL_ERR_PARAM;
    }
    /* 组件层本身不直接认识 HAL，只把请求转交给桥接层的单字节发送函数。 */
    return uart->bridge->write_byte(uart->context, byte);
}

/* 函数说明：通过桥接接口发送一段字节流。 */
osal_status_t periph_uart_write(periph_uart_t *uart, const uint8_t *data, uint32_t length) {
    osal_status_t status;
    uint32_t i;

    if ((!periph_uart_validate_handle(uart)) || (data == NULL)) {
        return OSAL_ERR_PARAM;
    }

    for (i = 0U; i < length; ++i) {
        /* 多字节发送本质上是循环调用单字节桥接。 */
        status = periph_uart_write_byte(uart, data[i]);
        if (status != OSAL_OK) {
            /* 只要某一个字节发送失败，就把错误立刻返回给上层。 */
            return status;
        }
    }

    return OSAL_OK;
}

/* 函数说明：通过桥接接口发送一个字符串。 */
osal_status_t periph_uart_write_string(periph_uart_t *uart, const char *str) {
    const char *cursor;

    if ((!periph_uart_validate_handle(uart)) || (str == NULL)) {
        return OSAL_ERR_PARAM;
    }

    cursor = str;
    while (*cursor != '\0') {
        /* 字符串发送按 C 风格字符串逐字节发到 '\0' 结束。 */
        osal_status_t status = periph_uart_write_byte(uart, (uint8_t)(*cursor));
        if (status != OSAL_OK) {
            return status;
        }
        ++cursor;
    }

    return OSAL_OK;
}

/*
 * 绑定控制台只是在组件层登记“当前标准输出应走哪个 USART 实例”，
 * 不会转移该对象的所有权。
 */
/* 函数说明：将指定 USART 对象绑定为当前控制台后端。 */
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
        /* 这里只提示“控制台后端被替换”，不会阻止重新绑定。 */
        periph_uart_report("console backend has been replaced by another USART instance");
    }
    /* 绑定后，后面的 fputc/printf 都会优先走这个 USART 实例。 */
    s_console_uart = uart;
    return OSAL_OK;
}

/* 函数说明：获取当前已绑定的控制台 USART 对象。 */
periph_uart_t *periph_uart_get_console(void) {
    return s_console_uart;
}

/* 函数说明：为标准输出重定向提供单字符发送。 */
int periph_uart_fputc(int ch, FILE *f) {
    (void)f;

    if (s_console_uart == NULL) {
        /* 没有绑定 console 时，标准输出重定向直接失败。 */
        return EOF;
    }

    if (periph_uart_write_byte(s_console_uart, (uint8_t)ch) != OSAL_OK) {
        return EOF;
    }

    /* 与标准库 fputc 约定一致：成功时返回写出的字符。 */
    return ch;
}

#endif /* OSAL_CFG_ENABLE_USART */




