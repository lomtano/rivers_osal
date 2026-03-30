# OSAL 移植指南

本文说明如何把当前这套 OSAL 移植到另一颗 32 位 MCU 上。

## 1. 复制目录

至少复制：

- `Middleware/osal/system`
- `Middleware/osal/platform`
- `Middleware/osal/components`

## 2. 加入头文件路径

最少加入：

- `Middleware/osal/system/Inc`
- `Middleware/osal/platform`
- `Middleware/osal/platform/example/<your_chip>`
- `Middleware/osal/components/periph/usart/Inc`
- `Middleware/osal/components/periph/flash/Inc`

## 3. 加入源文件

最少加入：

- `Middleware/osal/system/Src/*.c`
- `Middleware/osal/platform/example/<your_chip>/osal_platform_<your_chip>.c`
- `Middleware/osal/components/periph/usart/Src/*.c`
- `Middleware/osal/components/periph/flash/Src/*.c`

模板文件 `platform/osal_platform_cortexm.c` 一般不加入正式工程编译，它主要用来指导你填写自己的平台文件。

`osal_integration_stm32f4.c` 也是示例文件，默认不要求加入编译。

## 4. 主循环接入

主循环保持尽量简单：

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

## 5. 时基接入

在系统时基中断里调用：

```c
void SysTick_Handler(void)
{
    sdk_tick_handler();
    osal_tick_handler();
}
```

如果目标芯片的系统时基不叫 `SysTick`，也没有关系。  
只要你能在一个周期性系统时基中断里调用 `osal_tick_handler()` 即可。

## 6. 平台层该写什么

平台层只做桥接，不写复杂算法。

### 6.1 串口桥接

你只需要提供：

- 串口上下文
- 单字节发送函数

### 6.2 系统时基原始读接口

你只需要提供：

- 时钟频率
- 重装值
- 当前计数值
- 是否使能
- 是否发生过一次归零事件

OSAL 的 `system` 层会基于这些原始值自己完成：

- `osal_timer_delay_us()`
- `osal_timer_delay_ms()`
- `osal_timer_get_uptime_us()`
- `osal_timer_get_tick()`
- 软件定时器调度
- 回绕安全比较

### 6.3 中断接口

你只需要接好：

- `osal_irq_is_in_isr()`
- `osal_irq_disable()`
- `osal_irq_enable()`
- `osal_irq_restore()`

### 6.4 Flash 桥接

根据目标芯片能力填写：

- `unlock`
- `lock`
- `erase`
- `read`
- `write_u8`
- `write_u16`
- `write_u32`
- `write_u64`

## 7. 推荐顺序

建议按下面顺序调通：

1. 先调通 `osal_tick_handler()`
2. 再确认 `osal_timer_get_tick()` 递增正常
3. 再验证一个最小任务可以运行
4. 再验证消息队列
5. 再验证软件定时器
6. 最后接 USART 和 Flash 组件
