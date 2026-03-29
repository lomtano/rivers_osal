# OSAL Porting Guide

This document describes how to move `middleware/osal` into another 32-bit MCU project
such as STM32, GD32, or N32.

## 1. Copy the folder

Copy the whole `middleware/osal/` folder.

At minimum you usually keep:

- `middleware/osal/system`
- `middleware/osal/components`

If you also want the STM32F4 reference, keep:

- `middleware/osal/examples`

## 2. Add include paths

Add these include paths to the target project:

- `middleware/osal/system/Inc`
- `middleware/osal/components/periph/usart/Inc`
- `middleware/osal/components/periph/flash/Inc`

If you want the STM32F4 demo too:

- `middleware/osal/examples/stm32f4`

## 3. Add source files

Add these source files:

- `middleware/osal/system/Src/*.c`
- `middleware/osal/components/periph/usart/Src/*.c`
- `middleware/osal/components/periph/flash/Src/*.c`

Optional demo files:

- `middleware/osal/examples/stm32f4/*.c`

## 4. Implement the platform IRQ hooks

The OSAL core only requires a very small IRQ port:

```c
uint32_t osal_irq_disable(void);
void osal_irq_enable(void);
void osal_irq_restore(uint32_t prev_state);
bool osal_irq_is_in_isr(void);
```

Put these in your platform adapter, not in application code.

## 5. Configure one 1us timer interrupt

The timer module now uses only one input path, similar to `HAL_IncTick()`:

```c
void TIMx_IRQHandler(void) {
    osal_timer_inc_tick();
}
```

With that single ISR call, OSAL automatically maintains:

- a 32-bit wrapping microsecond counter
- a 32-bit wrapping millisecond tick
- microsecond busy delay
- software timer time base
- timeout handling used by other OSAL services

You do not need to register a custom time-provider callback anymore.

## 6. Optional: replace the default OSAL heap

If you do not want to use the default internal heap:

```c
static uint8_t g_osal_heap[8192];

void board_osal_heap_init(void) {
    osal_mem_init(g_osal_heap, sizeof(g_osal_heap));
}
```

Call this before creating tasks, queues, mutexes, events, timers, or components.

## 7. Main loop

Keep the application loop minimal:

```c
while (1) {
    osal_run();
}
```

`osal_run()` already dispatches software timer callbacks internally.

## 8. USART bridge porting

USART lives at:

- `middleware/osal/components/periph/usart`

The component API still uses the existing `periph_uart_*` names for compatibility.

The platform only needs to provide one byte-send bridge:

```c
static osal_status_t board_uart_write_byte(void *context, uint8_t byte) {
    board_uart_handle_t *uart = (board_uart_handle_t *)context;
    return (sdk_uart_send_byte(uart, byte) == SDK_OK) ? OSAL_OK : OSAL_ERROR;
}
```

Then bind it:

```c
static const periph_uart_bridge_t uart_bridge = {
    .write_byte = board_uart_write_byte
};

periph_uart_t *uart = periph_uart_create(&uart_bridge, &board_uart);
periph_uart_bind_console(uart);
```

And redirect `printf`:

```c
int fputc(int ch, FILE *f) {
    return periph_uart_fputc(ch, f);
}
```

## 9. Flash bridge porting

Flash lives at:

- `middleware/osal/components/periph/flash`

The bridge may implement any combination of:

- `unlock`
- `lock`
- `erase`
- `read`
- `write_u8`
- `write_u16`
- `write_u32`
- `write_u64`

Different MCUs expose different write widths. That is expected.
The upper layer always calls `periph_flash_write()`, and the component chooses the
widest valid write operation based on address alignment and available callbacks.

## 10. Queue porting notes

`osal_queue` is a generic fixed-item queue, like a small FreeRTOS queue.
It is not limited to `uint8_t` messages.

Examples:

- Pointer queue: `item_size = sizeof(my_msg_t *)`
- Struct queue: `item_size = sizeof(my_msg_t)`
- Fixed array queue:

```c
typedef uint8_t can_frame_t[8];
osal_queue_t *q = osal_queue_create(16U, sizeof(can_frame_t));
```

The queue copies `item_size` bytes per item internally.

## 11. Recommended validation order

1. Verify `osal_irq_*`
2. Verify `osal_timer_get_tick()` increments
3. Verify one minimal task runs under `osal_run()`
4. Verify USART output
5. Verify software timer callbacks
6. Verify queue send/receive
7. Verify flash component operations
