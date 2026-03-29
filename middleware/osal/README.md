# OSAL Middleware

This `middleware/osal` tree is a lightweight bare-metal OSAL skeleton for 32-bit MCUs.
The goal is FreeRTOS-like portability, but with a smaller cooperative scheduler and a
very thin platform porting surface.

## Layout

```text
middleware/osal/
|-- system/
|   |-- Inc/
|   `-- Src/
|-- components/
|   |-- periph/
|   |   |-- usart/
|   |   `-- flash/
|   `-- README.md
|-- examples/
|   `-- stm32f4/
|-- README.md
|-- PORTING_GUIDE.md
|-- USAGE_EXAMPLES.md
`-- CHANGELOG.md
```

## Layers

- `system/`
  Core OSAL services: task, queue, event, mutex, memory, interrupt abstraction, and timer.
- `components/`
  Reusable higher-level components. Today this contains `periph/`, and later it can also
  hold `rtt/`, `bootloader/`, or other standalone modules.
- `components/periph/`
  Peripheral bridge components such as `usart/` and `flash/`.
- `examples/`
  Platform examples and integration demos.

## Current capabilities

- Cooperative task scheduler
- Generic fixed-item message queues
- Event and mutex primitives
- Unified static heap in `osal_mem`
- HAL-style millisecond tick from a 1us timer interrupt
- Busy-wait microsecond delay
- One-shot and periodic software timers
- IRQ enable/disable and ISR-context query hooks
- USART bridge component based on a single-byte send callback
- Flash bridge component with erase/read and multiple write widths

## Timing model

The timer model is intentionally simple:

- The platform configures one hardware timer interrupt at `1us`.
- That ISR calls `osal_timer_inc_tick()`.
- OSAL internally derives:
  - `osal_timer_get_uptime_us()`
  - `osal_timer_get_uptime_ms()`
  - `osal_timer_get_tick()`
  - `osal_timer_delay_us()`
  - software timer expiration checks

No external time-provider callback is needed anymore.

## Main loop

```c
while (1) {
    osal_run();
}
```

`osal_run()` already polls software timers internally, so the application loop stays clean.

## Documentation

- Porting steps: `middleware/osal/PORTING_GUIDE.md`
- Usage examples: `middleware/osal/USAGE_EXAMPLES.md`
- Change history: `middleware/osal/CHANGELOG.md`
