# USART Component

这是 OSAL 组件层里的串口桥接组件目录。

## 目录

- `Inc/periph_uart.h`
- `Src/periph_uart.c`

## 为什么目录名叫 `usart`

这层以后不只会放“外设”概念下的内容，还可能继续增加：

- `rtt/`
- `bootloader/`
- `storage/`

所以组件层按“一个能力一个目录”的方式组织会更清晰。
当前串口组件目录使用 `usart/`，但 API 名称先保留 `periph_uart_*`，这样不会打断现有示例和移植代码。

## 当前能力

- 绑定单字节发送桥接函数
- 发送单字节
- 发送字符串
- 发送数组
- 挂接 `printf` / `fputc`
