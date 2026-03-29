# OSAL Usage Examples

这份文档对应 `middleware/osal/examples/stm32f4/osal_integration_stm32f4.c` 里的示例内容。

## 1. 两个任务无阻塞点两个灯

思路是每个任务只做一件事：

- 读取当前 `osal_timer_get_tick()`
- 判断是否到自己的翻转时间
- 到时间就翻转 LED
- 没到时间就立即返回

这样任务不会阻塞调度器，也不会像 `HAL_Delay()` 那样卡住主循环。

## 2. 队列生产者/消费者

示例里有两个任务：

- 生产者每 1000ms 往队列发送一个递增计数值
- 消费者每轮调度都尝试非阻塞收消息，收到后打印

这种模式后面很适合扩展成：

- 串口异步发送缓冲
- 传感器采样消息分发
- 状态机事件投递

## 3. 单次和周期性软件定时器打印

示例同时演示了：

- 单次软件定时器：2 秒后打印一次
- 周期性软件定时器：每 1 秒打印一次

软件定时器回调由 `osal_run()` 内部触发，不需要你在主循环中手动再调 `osal_timer_poll()`。

## 4. USART 组件桥接

`periph_uart` 的核心思想是：

- 上层逻辑不关心 MCU SDK
- 平台层只需要提供“发送单字节”函数

当前目录位置是：

- `middleware/osal/components/usart`

例如：

```c
static osal_status_t board_uart_write_byte(void *context, uint8_t byte) {
    board_uart_handle_t *uart = (board_uart_handle_t *)context;
    return (sdk_uart_send_byte(uart, byte) == SDK_OK) ? OSAL_OK : OSAL_ERROR;
}
```

然后：

```c
periph_uart_t *uart = periph_uart_create(&uart_bridge, &board_uart);
periph_uart_bind_console(uart);
```

如果你需要 `printf`：

```c
int fputc(int ch, FILE *f) {
    return periph_uart_fputc(ch, f);
}
```

## 5. Flash 组件桥接

`periph_flash` 的目标是把不同 MCU 的 Flash 差异收敛在桥接层里，包括：

- 解锁
- 上锁
- 擦除
- 读取
- 按不同宽度编程

如果芯片只支持 halfword 写：

- 只实现 `write_u16`

如果芯片支持 word 或 doubleword 写：

- 再实现 `write_u32`
- 或 `write_u64`

上层使用方式保持一致：

```c
periph_flash_unlock(flash);
periph_flash_erase(flash, demo_addr, demo_len);
periph_flash_write(flash, demo_addr, payload, payload_len);
periph_flash_lock(flash);
```

## 6. STM32F4 示例分工

当前 STM32F4 示例里：

- `osal_platform_stm32f4.c/.h`
  负责 TIM2 1us tick、IRQ、UART bridge、Flash bridge、LED 钩子
- `osal_integration_stm32f4.c`
  负责任务、队列、软件定时器和 Flash 示例调用

## 7. 推荐初始化顺序

```c
void app_init(void) {
    board_osal_heap_init();      // 可选
    board_osal_time_init();      // 二选一：1us ISR 或硬件 us provider
    app_uart_init();
    app_flash_init();
    app_led_demo_init();
    app_queue_demo_init();
    app_timer_demo_init();
}
```

主循环：

```c
while (1) {
    osal_run();
}
```
