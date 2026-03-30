# OSAL 跨 MCU 最小移植模板

本文只保留最小移植步骤，目标是让用户尽量少写代码，只负责把 OSAL 和目标 MCU 的 SDK 接起来。

## 1. 适用范围

这套 OSAL 的 `system` 层负责：

- 统一状态码
- 协作式任务调度
- 队列、事件、互斥量
- 静态堆管理
- 毫秒 Tick 维护
- 微秒级延时和微秒运行时间换算
- 软件定时器
- 超时和计数回绕处理

平台适配层只负责：

- 初始化底层外设或时基
- 提供原始 Tick 计数源读接口
- 提供中断开关接口
- 挂接串口、Flash 等桥接函数

也就是说，复杂逻辑应放在 `system`，`platform` 只做桥接，不放算法。

## 2. 复制目录

至少复制下面两个目录：

- `Middleware/osal/system`
- `Middleware/osal/components`

如果要参考现成平台示例，再复制：

- `Middleware/osal/examples`

## 3. 工程需要加入的路径

最少加入这些头文件路径：

- `Middleware/osal/system/Inc`
- `Middleware/osal/components/periph/usart/Inc`
- `Middleware/osal/components/periph/flash/Inc`

如果要使用示例平台层，再加入：

- `Middleware/osal/examples/<your_platform>`

## 4. 工程需要加入的源文件

最少加入这些源文件：

- `Middleware/osal/system/Src/*.c`
- `Middleware/osal/components/periph/usart/Src/*.c`
- `Middleware/osal/components/periph/flash/Src/*.c`

如果要复用某个平台示例，再把对应 `examples` 目录下的 `.c` 一起加入。

## 5. 主循环最小接入方式

系统初始化阶段调用一次：

```c
#include "osal.h"

int main(void)
{
    board_init();
    osal_init();

    while (1) {
        osal_run();
    }
}
```

`osal_init()` 会调用平台层初始化钩子，并同步当前 Tick 计数源配置。

## 6. 中断入口最小接入方式

在系统周期 Tick 中断里调用：

```c
void SysTick_Handler(void)
{
    sdk_tick_handler();
    osal_tick_handler();
}
```

如果你的 MCU 不叫 `SysTick`，也没有关系，只要在你自己的周期性系统时基中断里调用 `osal_tick_handler()` 即可。

`osal_tick_handler()` 只负责维护 OSAL 的粗粒度 Tick。  
微秒级细分时间、毫秒换算、软件定时器最近到期判断，都在 `system` 层内部完成。

## 7. 平台层必须实现的接口

### 7.1 平台初始化钩子

```c
void osal_platform_init(void);
```

这里建议只做：

- 挂接串口桥接
- 挂接 Flash 桥接
- 准备系统 Tick 计数源

不要在这里放复杂计时算法。

### 7.2 Tick 计数源桥接

```c
typedef struct {
    uint32_t (*get_counter_clock_hz)(void);
    uint32_t (*get_reload_value)(void);
    uint32_t (*get_current_value)(void);
    bool (*is_enabled)(void);
    bool (*has_elapsed)(void);
} osal_tick_source_t;

const osal_tick_source_t *osal_platform_get_tick_source(void);
```

这组接口只需要返回原始硬件信息：

- 计数器输入时钟频率
- 周期重装值
- 当前计数值
- 当前是否使能
- 是否已经发生过一次归零事件

`system` 层会基于这些原始数据自动计算：

- `osal_timer_delay_us()`
- `osal_timer_delay_ms()`
- `osal_timer_get_uptime_us()`
- `osal_timer_get_tick()`
- 软件定时器最近到期点
- 回绕安全的超时比较

用户不需要自己写这些换算逻辑。

## 8. Cortex-M 上的推荐做法

如果目标是 STM32、GD32、N32 这类 Cortex-M MCU，推荐直接用系统时基计数器作为 OSAL 的 Tick 源。

平台层只需要把下面这些原始读接口接上：

```c
static uint32_t board_tick_get_clock_hz(void)
{
    return HAL_RCC_GetHCLKFreq();
}

static uint32_t board_tick_get_reload_value(void)
{
    return SysTick->LOAD + 1U;
}

static uint32_t board_tick_get_current_value(void)
{
    return SysTick->VAL;
}

static bool board_tick_is_enabled(void)
{
    return (SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) != 0U;
}

static bool board_tick_has_elapsed(void)
{
    return (SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) != 0U;
}
```

注意：这里展示的是 Cortex-M 示例，不是强制要求名字必须叫 `SysTick`。  
如果别的 MCU SDK 对象名不同，只要把这几项原始能力映射出来即可。

## 9. 中断接口也只需要做桥接

OSAL 真正需要的平台中断接口很少：

```c
uint32_t osal_irq_disable(void);
void osal_irq_enable(void);
void osal_irq_restore(uint32_t prev_state);
bool osal_irq_is_in_isr(void);
```

平台层只需要把它们映射到本 MCU 的关中断、开中断和 ISR 状态判断接口。

## 10. 串口组件最小桥接模板

串口组件只要求底层提供“发送单字节”能力：

```c
static osal_status_t board_uart_write_byte(void *context, uint8_t byte)
{
    board_uart_handle_t *uart = (board_uart_handle_t *)context;
    return (sdk_uart_send_byte(uart, byte) == SDK_OK) ? OSAL_OK : OSAL_ERROR;
}
```

然后桥接：

```c
static const periph_uart_bridge_t s_uart_bridge = {
    .write_byte = board_uart_write_byte
};

periph_uart_t *uart = periph_uart_create(&s_uart_bridge, &board_uart);
periph_uart_bind_console(uart);
```

如果需要 `printf` 重定向：

```c
int fputc(int ch, FILE *f)
{
    return periph_uart_fputc(ch, f);
}
```

## 11. Flash 组件最小桥接模板

Flash 组件根据目标 MCU 能力，选择实现以下任意组合：

- `unlock`
- `lock`
- `erase`
- `read`
- `write_u8`
- `write_u16`
- `write_u32`
- `write_u64`

上层统一走 `periph_flash_write()`，组件内部会根据地址对齐和桥接能力自动选择当前能用的最宽写法。

## 12. 队列成员类型说明

`osal_queue` 是固定成员大小的泛型消息队列，不只支持 `uint8_t`。

可以直接放：

- 结构体
- 指针
- 定长数组

示例：

```c
typedef struct {
    uint16_t id;
    uint8_t data[8];
} app_msg_t;

osal_queue_t *q1 = osal_queue_create(8U, sizeof(app_msg_t));
osal_queue_t *q2 = osal_queue_create(8U, sizeof(app_msg_t *));
```

如果完全不想让消息缓存来自 OSAL 堆，推荐使用：

```c
osal_queue_create_static(buffer, length, item_size);
```

## 13. 推荐验证顺序

建议按下面顺序排查，最省时间：

1. 先确认 `osal_tick_handler()` 确实在周期中断里执行
2. 再确认 `osal_timer_get_tick()` 持续递增
3. 再验证一个最小任务能否在 `osal_run()` 下工作
4. 再接入 `osal_task_sleep()`
5. 再验证软件定时器
6. 再验证串口输出
7. 最后验证消息队列和 Flash 组件

如果前三步不通，就不要继续往上层追。
