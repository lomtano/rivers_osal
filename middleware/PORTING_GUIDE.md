# Middleware Porting Guide

这份文档面向把 `middleware/osal` 和 `middleware/components` 移植到 STM32、GD32、N32 等 32 位 MCU 裸机工程。

## 1. 复制目录

至少复制这些目录：

- `middleware/osal/Inc`
- `middleware/osal/Src`
- `middleware/components/periph`
- `middleware/components/flash`

可参考的适配示例：

- `middleware/osal/examples/stm32f4`

## 2. 加入头文件路径与源文件

头文件路径：

- `middleware/osal/Inc`
- `middleware/components/periph/Inc`
- `middleware/components/flash/Inc`
- `middleware/osal/examples/stm32f4`

源文件：

- `middleware/osal/Src/*.c`
- `middleware/components/periph/Src/*.c`
- `middleware/components/flash/Src/*.c`

## 3. 先补平台相关的最小能力

OSAL 核心真正依赖平台的只有两类东西：

1. 中断临界区
2. 时间基准

至少要实现这三个函数：

```c
uint32_t osal_irq_disable(void);
void osal_irq_restore(uint32_t prev_state);
bool osal_irq_is_in_isr(void);
```

## 4. 时间基准接入

### 方案 A：1us 周期中断

如果你准备了一个 1us 周期的定时器中断，那么 ISR 里只保留这一句：

```c
void TIMx_IRQHandler(void) {
    osal_timer_inc_tick();
}
```

这种方式下：

- `osal_timer_get_uptime_us()` 是 32 位回绕的 us 计数
- `osal_timer_get_tick()` 是 HAL 风格的 ms tick
- `osal_task_sleep()`、队列超时、软件定时器都可以工作

### 方案 B：直接绑定硬件 us 计数器

如果平台已经有自由运行的高精度硬件计数器，可以直接绑定：

```c
static uint32_t board_get_us(void) {
    return my_hw_timer_get_us();
}

void board_osal_time_init(void) {
    osal_timer_set_us_provider(board_get_us);
}
```

这种方式下不需要再调用 `osal_timer_inc_tick()`。

## 5. 可选：替换默认 OSAL 堆

如果你不想使用默认 `OSAL_HEAP_SIZE` 对应的大数组，可以在系统早期初始化：

```c
static uint8_t g_osal_heap[8192];

void board_osal_heap_init(void) {
    osal_mem_init(g_osal_heap, sizeof(g_osal_heap));
}
```

建议在创建任务、队列、事件、互斥量、软件定时器、组件之前完成这一步。

## 6. 主循环

主循环保持最简单：

```c
while (1) {
    osal_run();
}
```

`osal_run()` 内部已经会调用 `osal_timer_poll()`，不需要你再手工调一次。

## 7. UART 组件移植

UART 组件只要求你提供一个“发送单字节”的桥接函数。

```c
static osal_status_t board_uart_write_byte(void *context, uint8_t byte) {
    my_uart_handle_t *uart = (my_uart_handle_t *)context;
    return (sdk_uart_send_byte(uart, byte) == SDK_OK) ? OSAL_OK : OSAL_ERROR;
}
```

然后创建组件：

```c
static const periph_uart_bridge_t uart_bridge = {
    .write_byte = board_uart_write_byte
};

periph_uart_t *uart = periph_uart_create(&uart_bridge, &board_uart);
periph_uart_bind_console(uart);
```

如果要做 `printf` 重定向：

```c
int fputc(int ch, FILE *f) {
    return periph_uart_fputc(ch, f);
}
```

## 8. Flash 组件移植

Flash 组件桥接时重点实现：

- `unlock`
- `lock`
- `erase(address, length)`
- `read`
- `write_u8`
- `write_u16`
- `write_u32`
- `write_u64`

不是每个 MCU 都要把 4 个写宽度全部实现完。比如：

- 只支持 halfword 写，就实现 `write_u16`
- 支持 word 写，就实现 `write_u32`
- 支持 doubleword 写，就实现 `write_u64`

上层调用 `periph_flash_write()` 时，会根据地址对齐和桥接能力自动选择当前最合适的写宽度。

## 9. 推荐移植顺序

建议按这个顺序验证：

1. `osal_irq_*` 正常
2. `osal_timer_get_tick()` 正常增长
3. `osal_run()` + 一个最小任务正常运行
4. UART 组件能打印
5. 软件定时器能触发
6. 队列生产者/消费者正常
7. Flash 组件示例正常

## 10. 常见问题

### `delay_us()` 不准

原因通常是底层没有真正的 us 级时间源。

处理方式：

- 使用 1us 周期定时器中断调用 `osal_timer_inc_tick()`
- 或者绑定一个真实的硬件 us 计数器到 `osal_timer_set_us_provider()`

### 软件定时器不触发

检查：

- 主循环是否持续调用 `osal_run()`
- 时间基准是否真的在持续更新
- 是否成功 `osal_timer_start()`

### UART 组件移植后不能打印

检查：

- 桥接里是否真的接上了目标 SDK 的单字节发送函数
- `context` 传入的是否是正确的串口句柄
- 单字节发送失败时是否正确返回了错误码
