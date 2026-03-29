# OSAL Components

`components/` is reserved for reusable modules that sit above the OSAL core.

Current layout:

- `periph/`
  Peripheral bridge components

Future siblings can be added here too, for example:

- `rtt/`
- `bootloader/`
- `storage/`

This keeps `system/` focused on the OSAL core while `components/` holds optional,
portable building blocks.
