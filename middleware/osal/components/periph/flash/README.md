# Flash Bridge Component

Location:

- `components/periph/flash/Inc/periph_flash.h`
- `components/periph/flash/Src/periph_flash.c`

## Purpose

This component hides MCU-specific internal flash differences behind one reusable API.

Typical differences handled in the bridge:

- unlock/lock sequence
- erase granularity
- write width support
- raw read path

## Bridge callbacks

The bridge can implement any combination of:

- `unlock`
- `lock`
- `erase`
- `read`
- `write_u8`
- `write_u16`
- `write_u32`
- `write_u64`

The generic `periph_flash_write()` helper automatically selects the widest legal write
that matches the current address alignment and installed bridge callbacks.
