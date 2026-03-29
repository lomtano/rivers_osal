# OSAL Usage Examples

This document matches the STM32F4 demo at:

- `middleware/osal/examples/stm32f4/osal_integration_stm32f4.c`

## 1. Two non-blocking LED tasks

The demo creates two cooperative tasks.
Each task:

- reads `osal_timer_get_tick()`
- checks whether its own next deadline has arrived
- toggles one LED when due
- returns immediately otherwise

This keeps the scheduler responsive and avoids blocking with `HAL_Delay()`.

## 2. Generic queue producer/consumer

The queue demo uses a struct message, not a byte-only message:

```c
typedef struct {
    uint32_t sequence;
    uint8_t payload[8];
} queue_message_t;
```

The producer sends one `queue_message_t` every 1000 ms.
The consumer receives the struct and prints its content.

The same queue API can also store:

- pointers
- fixed-size arrays
- other structs

as long as the queue is created with the correct `item_size`.

## 3. One-shot and periodic software timers

The demo creates:

- one one-shot timer that fires after 2 seconds
- one periodic timer that fires every 1 second

Timer callbacks run from the `osal_run()` polling path, so the application loop stays:

```c
while (1) {
    osal_run();
}
```

## 4. USART bridge usage

USART is now located under:

- `middleware/osal/components/periph/usart`

The bridge only needs one byte-send callback from the target SDK.
After binding the component, `printf` can be redirected through:

```c
int fputc(int ch, FILE *f) {
    return periph_uart_fputc(ch, f);
}
```

## 5. Flash bridge usage

Flash is located under:

- `middleware/osal/components/periph/flash`

Typical usage:

```c
periph_flash_unlock(flash);
periph_flash_erase(flash, demo_addr, demo_len);
periph_flash_write(flash, demo_addr, payload, payload_len);
periph_flash_lock(flash);
```

The width-specific writes stay hidden in the bridge implementation.

## 6. STM32F4 example split

The STM32F4 example is split on purpose:

- `osal_platform_stm32f4.c/.h`
  Platform adaptation, timer IRQ, USART bridge, flash bridge, LED hooks
- `osal_integration_stm32f4.c`
  Example tasks, queue demo, software timer demo, flash demo usage

This same split is recommended when you port to GD32 or N32.
