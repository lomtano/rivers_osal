# Peripheral Components

`components/periph/` holds bridge-style peripheral components.

Current modules:

- `usart/`
- `flash/`

Each module hides MCU SDK details behind a narrow callback table so the upper layer
can stay portable.
