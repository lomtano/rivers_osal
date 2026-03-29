# OSAL Porting Guide

这份文档面向把 `middleware/osal` 直接复制到 STM32、GD32、N32 等 32 位 MCU 工程中使用。

## 1. 复制目录

至少复制整个 `middleware/osal/` 目录。

如果你只想先接系统层，也至少要保留这些目录：

- `middleware/osal/system`
- `middleware/osal/components`

如果你还想参考平台示例，再额外保留：

- `middleware/osal/examples`

## 2. 加入头文件路径

建议加入这些 include path：

- `middleware/osal/system/Inc`
- `middleware/osal/components/periph/Inc`
- `middleware/osal/components/flash/Inc`
- `middleware/osal/examples/stm32f4`

## 3. 加入源文件

至少加入：

- `middleware/osal/system/Src/*.c`
- `middleware/osal/components/periph/Src/*.c`
- `middleware/osal/components/flash/Src/*.c`

如果你需要参考 STM32F4 示例，再加入：

- `middleware/osal/examples/stm32f4/*.c`

## 4. 先补平台最小能力

OSAL 真正依赖平台的核心能力只有两类：

1. 中断控制
2. 时间基准

你至少要实现：

```c
uint32_t osal_irq_disable(void);
void osal_irq_enable(void);
void osal_irq_restore(uint32_t prev_state);
bool osal_irq_is_in_isr(void);
```

建议这些都写在你的平台适配文件里，而不是写进业务文件。

## 5. 时间基准接法

### 方案 A：1us 周期中断

如果你准备了一个周期为 1us 的定时器中断，那么 ISR 中只需要：

```c
void TIMx_IRQHandler(void) {
    osal_timer_inc_tick();
}
```

这种方式下：

- `osal_timer_get_uptime_us()` 提供 32 位回绕的 us 计数
- `osal_timer_get_tick()` 提供 HAL 风格的 ms tick
- `osal_task_sleep()`、队列超时、软件定时器都可以正常工作

### 方案 B：绑定硬件 us 计数器

如果平台已经有稳定的自由运行 us 计数器，可以这样绑定：

```c
static uint32_t board_get_us(void) {
    return my_hw_timer_get_us();
}

void board_osal_time_init(void) {
    osal_timer_set_us_provider(board_get_us);
}
```

这种方式下就不需要再调 `osal_timer_inc_tick()`。

## 6. 可选：替换默认 OSAL 堆

如果不想使用默认静态堆，可以在系统早期初始化：

```c
static uint8_t g_osal_heap[8192];

void board_osal_heap_init(void) {
    osal_mem_init(g_osal_heap, sizeof(g_osal_heap));
}
```

建议在创建任务、队列、互斥量、事件、软件定时器、组件之前完成这一步。

## 7. 主循环

最小主循环保持这样就可以：

```c
while (1) {
    osal_run();
}
```

`osal_run()` 内部已经会自动执行软件定时器轮询。

## 8. UART 组件移植

UART 组件只要求你提供一个“发送单字节”的桥接函数：

```c
static osal_status_t board_uart_write_byte(void *context, uint8_t byte) {
    my_uart_handle_t *uart = (my_uart_handle_t *)context;
    return (sdk_uart_send_byte(uart, byte) == SDK_OK) ? OSAL_OK : OSAL_ERROR;
}
```

然后创建并挂载：

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

## 9. Flash 组件移植

Flash 组件桥接时重点实现：

- `unlock`
- `lock`
- `erase(address, length)`
- `read`
- `write_u8`
- `write_u16`
- `write_u32`
- `write_u64`

并不是所有芯片都必须实现全部写宽度。比如：

- 只支持 halfword 写，就实现 `write_u16`
- 支持 word 写，就实现 `write_u32`
- 支持 doubleword 写，就实现 `write_u64`

上层统一调用 `periph_flash_write()` 即可，组件会根据地址对齐和桥接能力自动选择最合适的写宽度。

## 10. 平台示例组织建议

参考 `middleware/osal/examples/stm32f4/` 的分工：

- `osal_platform_stm32f4.c/.h`
  只放平台适配和桥接
- `osal_integration_stm32f4.c`
  只放任务、队列、软件定时器、Flash 示例用法

建议你后续迁移到 GD32/N32 时也继续保持这种结构。

## 11. 推荐验证顺序

建议按这个顺序验证：

1. `osal_irq_*` 正常
2. `osal_timer_get_tick()` 正常增长
3. `osal_run()` + 一个最小任务正常运行
4. UART 组件打印正常
5. 软件定时器触发正常
6. 队列生产者/消费者正常
7. Flash 组件示例正常
