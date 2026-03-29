# OSAL 移植指南

本文说明如何把 `middleware/osal` 移植到另一套 32 位 MCU 工程中，例如 STM32、GD32、N32。

## 1. 复制目录

把整个 `middleware/osal/` 目录复制到目标工程中。

最少通常保留：

- `middleware/osal/system`
- `middleware/osal/components`

如果你还想保留 STM32F4 参考示例，也一起复制：

- `middleware/osal/examples`

## 2. 添加头文件路径

给工程加入以下头文件路径：

- `middleware/osal/system/Inc`
- `middleware/osal/components/periph/usart/Inc`
- `middleware/osal/components/periph/flash/Inc`

如果还要编译 STM32F4 示例，再加入：

- `middleware/osal/examples/stm32f4`

## 3. 添加源文件

把以下源文件加入工程：

- `middleware/osal/system/Src/*.c`
- `middleware/osal/components/periph/usart/Src/*.c`
- `middleware/osal/components/periph/flash/Src/*.c`

示例文件按需加入：

- `middleware/osal/examples/stm32f4/*.c`

## 4. 实现平台中断抽象

OSAL 系统层真正依赖的平台中断接口很少，只有下面四个：

```c
uint32_t osal_irq_disable(void);
void osal_irq_enable(void);
void osal_irq_restore(uint32_t prev_state);
bool osal_irq_is_in_isr(void);
```

这些函数应当放在平台适配层里，不要散落在应用代码里。

## 5. 接入 1us 中断源

`osal_timer` 现在的使用方式和 `HAL_IncTick()` 很接近：  
平台层只需要在固定 `1us` 周期的中断里调用一次 `osal_timer_inc_tick()`。

```c
void TIMx_IRQHandler(void) {
    osal_timer_inc_tick();
}
```

有了这一次调用后，OSAL 内部会自动维护：

- 32 位微秒计数
- 32 位毫秒 `tick`
- 微秒忙等待延时
- 软件定时器时间基准
- 其它 OSAL 服务使用到的超时回绕判断

你不再需要自己挂额外的计时回调函数。

## 6. STM32F4 模板适配方式

`middleware/osal/examples/stm32f4/osal_platform_stm32f4.h` 已经整理成模板骨架。

如果你使用通用 `TIMx`：

1. 修改这些宏，让它们匹配你的定时器资源：
   `OSAL_PLATFORM_TICK_TIM_INSTANCE`
   `OSAL_PLATFORM_TICK_TIM_IRQn`
   `OSAL_PLATFORM_TICK_TIM_CLK_ENABLE()`
   `OSAL_PLATFORM_TICK_TIM_APB_BUS`
2. 启动阶段调用 `osal_platform_tick_start()`
3. 在对应的 `TIMx_IRQHandler()` 里调用 `osal_platform_tick_irq_handler()`

如果你使用 `SysTick`：

1. 自己保证 `SysTick` 的中断周期就是 `1us`
2. 在 `SysTick_Handler()` 中调用 `osal_platform_systick_handler()`

一般来说，裸机项目更推荐使用独立通用定时器驱动 OSAL tick，避免把 `SysTick` 压到过高频率。

## 7. 可选：替换默认 OSAL 静态堆

如果你不想使用 `osal_mem` 内部自带的默认静态大数组，可以在启动早期换成你自己的静态缓冲区：

```c
static uint8_t g_osal_heap[8192];

void board_osal_heap_init(void) {
    osal_mem_init(g_osal_heap, sizeof(g_osal_heap));
}
```

这个缓冲区依然是静态内存，不是系统 `heap`。  
请在创建任务、队列、互斥量、事件、软件定时器和组件之前完成初始化。

## 8. 主循环

应用主循环尽量保持最简：

```c
while (1) {
    osal_run();
}
```

`osal_run()` 内部已经会处理软件定时器轮询。

## 9. USART 组件移植

`USART` 组件位于：

- `middleware/osal/components/periph/usart`

为了兼容之前代码，当前组件对外仍然使用 `periph_uart_*` 这一组 API 名称。

平台层只需要提供一个“发送单字节”的桥接函数：

```c
static osal_status_t board_uart_write_byte(void *context, uint8_t byte) {
    board_uart_handle_t *uart = (board_uart_handle_t *)context;
    return (sdk_uart_send_byte(uart, byte) == SDK_OK) ? OSAL_OK : OSAL_ERROR;
}
```

然后挂载桥接：

```c
static const periph_uart_bridge_t uart_bridge = {
    .write_byte = board_uart_write_byte
};

periph_uart_t *uart = periph_uart_create(&uart_bridge, &board_uart);
periph_uart_bind_console(uart);
```

若要重定向 `printf`，可直接写：

```c
int fputc(int ch, FILE *f) {
    return periph_uart_fputc(ch, f);
}
```

## 10. Flash 组件移植

`Flash` 组件位于：

- `middleware/osal/components/periph/flash`

桥接层可按目标 MCU 能力实现以下任意组合：

- `unlock`
- `lock`
- `erase`
- `read`
- `write_u8`
- `write_u16`
- `write_u32`
- `write_u64`

不同 MCU 支持的擦写粒度、可写宽度本来就不同，这属于正常差异。  
上层始终调用 `periph_flash_write()`，组件内部会根据地址对齐和已安装的函数指针，自动选择当前可用的最宽合法写法。

## 11. 队列移植说明

`osal_queue` 是一个泛型固定成员大小队列，思路和小型化的 FreeRTOS 队列类似，  
它并不只支持 `uint8_t`。

示例：

- 指针队列：`item_size = sizeof(my_msg_t *)`
- 结构体队列：`item_size = sizeof(my_msg_t)`
- 定长数组队列：

```c
typedef uint8_t can_frame_t[8];
osal_queue_t *q = osal_queue_create(16U, sizeof(can_frame_t));
```

队列内部每次按 `item_size` 拷贝消息，所以结构体、指针、数组本身都可以作为队列成员。

如果你希望 MCU 工程完全避免动态对象申请，建议优先使用：

```c
osal_queue_create_static(buffer, length, item_size);
```

这样消息缓存区完全由你自己的静态数组提供。

## 12. 建议验证顺序

1. 先验证 `osal_irq_*`
2. 再验证 `osal_timer_get_tick()` 是否正常递增
3. 再验证一个最小任务能否在 `osal_run()` 下正常运行
4. 再验证 USART 输出
5. 再验证软件定时器回调
6. 再验证队列收发
7. 最后验证 Flash 组件
