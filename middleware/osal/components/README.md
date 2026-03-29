# Components

这里放的是 OSAL 组件层的小型可复用抽象模块。

当前包含：

- `periph/`
  UART 单字节桥接组件
- `flash/`
  内部 Flash 解锁、擦除、编程桥接组件

这层的定位是：

- 属于 `middleware/osal` 的一部分
- 但不和系统调度内核强耦合
- 通过桥接模式适配不同 MCU SDK
