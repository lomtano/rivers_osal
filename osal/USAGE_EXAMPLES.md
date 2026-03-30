# OSAL 使用示例

本文对应的参考示例文件是：

- `osal/examples/stm32f4/osal_integration_stm32f4.c`

## 1. 两个无阻塞点灯任务

示例中创建了两个协作式任务。每个任务都会：

- 读取 `osal_timer_get_tick()`
- 判断自己的下一次翻转时间是否已到
- 到时就切换一次 LED
- 没到时立即返回

这样调度器始终保持响应，不需要使用 `HAL_Delay()` 一类阻塞式延时。

## 2. 泛型队列生产者/消费者

队列示例使用的是结构体消息，而不是单纯的字节流：

```c
typedef struct {
    uint32_t sequence;
    uint8_t payload[8];
} queue_message_t;
```

生产者每隔 `1000ms` 发送一个 `queue_message_t`，消费者取出后打印内容。

这一套队列接口同样可以存放：

- 指针
- 定长数组
- 其它结构体

只要创建队列时传入正确的 `item_size` 即可。

当前 STM32F4 示例里，队列演示默认使用的是：

```c
osal_queue_create_static(g_queue_storage, 8U, sizeof(queue_message_t));
```

也就是显式静态数组作为消息缓存区，更符合 MCU 裸机项目的内存使用习惯。

## 3. 单次和周期性软件定时器

示例里创建了两个软件定时器：

- 一个 `2s` 后触发一次的单次定时器
- 一个每 `1s` 触发一次的周期定时器

软件定时器回调在 `osal_run()` 内部轮询阶段触发，因此应用主循环只需保持：

```c
while (1) {
    osal_run();
}
```

## 4. USART 桥接组件使用方式

`USART` 组件位于：

- `osal/components/periph/usart`

平台层只需要提供目标 SDK 的“发送单字节”函数桥接。挂载成功后，
`printf` 重定向可以直接写成：

```c
int fputc(int ch, FILE *f) {
    return periph_uart_fputc(ch, f);
}
```

对上层来说，发送单字节、字符串、数组的接口就不需要再随着 MCU SDK 改动。

## 5. Flash 桥接组件使用方式

`Flash` 组件位于：

- `osal/components/periph/flash`

典型调用流程如下：

```c
periph_flash_unlock(flash);
periph_flash_erase(flash, demo_addr, demo_len);
periph_flash_write(flash, demo_addr, payload, payload_len);
periph_flash_lock(flash);
```

不同 MCU 的扇区大小、擦除粒度和支持的写入宽度都由桥接层自己处理，
上层不需要再关心具体用的是字节、半字、字还是双字写入。

## 6. STM32F4 示例文件职责划分

STM32F4 示例刻意拆成两部分：

- `osal_platform_stm32f4.c/.h`
  只负责平台适配，例如 `TIMx/SysTick` 接入、USART 桥接、Flash 桥接、LED 钩子
- `osal_integration_stm32f4.c`
  只负责业务示范，例如任务、队列、软件定时器、Flash 示例调用

后续移植到 GD32、N32 时，也建议保持同样的拆分方式。
