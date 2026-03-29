# Changelog

## 2026-03-29

### Structure

- Reorganized the OSAL tree into `system/`, `components/`, and `examples/`
- Added the `components/periph/` layer so peripheral modules no longer sit at the same level as future modules such as `rtt/` or `bootloader/`
- Moved USART to `middleware/osal/components/periph/usart/`
- Kept flash at `middleware/osal/components/periph/flash/`

### Core API

- Removed `osal_status.h` and centralized `osal_status_t` in `osal.h`
- Added explicit `osal_irq_enable()`
- Simplified the timer model to one `osal_timer_inc_tick()` entry called from a fixed `1us` timer ISR
- Kept `osal_timer_get_tick()` as the HAL-style wrapping millisecond tick

### Queue

- Reworked `osal_queue` into a generic fixed-item queue interface
- Added heap-backed `osal_queue_create(length, item_size)`
- Added caller-buffer-backed `osal_queue_create_static(buffer, length, item_size)`
- Clarified support for structs, pointers, and fixed-size arrays

### Components

- Kept the USART bridge API based on a single-byte send callback
- Kept the flash bridge API with width-aware write selection

### Examples and docs

- Updated the STM32F4 integration demo to use a struct message queue example
- Updated the porting guide for the new component hierarchy
- Updated the usage guide for the new queue and timer behavior
