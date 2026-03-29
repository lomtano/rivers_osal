# USART Bridge Component

Location:

- `components/periph/usart/Inc/periph_uart.h`
- `components/periph/usart/Src/periph_uart.c`

## Purpose

This component provides a small bridge for bare-metal console output and byte-stream
transmission without coupling the upper layer to one MCU SDK.

## Bridge requirement

The platform only needs to provide one callback:

- `write_byte`

Once that callback is mounted, the component can offer:

- single-byte send
- byte-array send
- string send
- `printf` retarget through `periph_uart_fputc()`

The public API still keeps the existing `periph_uart_*` naming for compatibility.
