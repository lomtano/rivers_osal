# OSAL 组件层

`components/` 用来放位于 OSAL 系统层之上的可复用小组件。

当前结构：

- `periph/`
  外设桥接组件

后续也可以继续在这里扩展：

- `rtt/`
- `bootloader/`
- `storage/`

这样 `system/` 可以始终专注于 OSAL 核心能力，而 `components/` 则承载那些可选、
可复用、可移植的小功能模块。
