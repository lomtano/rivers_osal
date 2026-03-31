# USART 桥接组件

位置：

- `components/periph/usart/Inc/periph_uart.h`
- `components/periph/usart/Src/periph_uart.c`

## 作用

这个组件用于在裸机场景下提供统一的串口输出桥接层，
让上层逻辑不再直接依赖某一家 MCU SDK。

## 桥接要求

平台层只需要提供一个回调：

- `write_byte`

挂载这个回调后，组件就可以统一提供：

- 单字节发送
- 字节数组发送
- 字符串发送
- 通过 `periph_uart_fputc()` 实现 `printf` 重定向

为了兼容已有代码，当前公开 API 仍然保留 `periph_uart_*` 这一组命名。
