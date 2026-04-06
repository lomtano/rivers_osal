# OSAL 说明

这套 `OSAL` 是一个面向 32 位 MCU 的轻量裸机框架，目标是：

- 不直接耦合具体 MCU SDK
- 用尽量少的代码搭出任务、时基、队列和基础同步能力
- 把移植修改尽量收敛到 `platform` 层

## 目录

```text
osal/
|-- system/
|   |-- Inc/
|   `-- Src/
|-- platform/
|   |-- osal_platform_cortexm.h
|   |-- osal_platform_cortexm.c
|   `-- example/
|       `-- stm32f4/
|           |-- osal_platform_stm32f4.h
|           |-- osal_platform_stm32f4.c
|           `-- osal_integration_stm32f4.c
|-- components/
|   |-- periph/
|   |   |-- flash/
|   |   `-- usart/
|   |-- RTT/
|   `-- KEY/
|-- README.md
`-- CHANGELOG.md
```

## 分层职责

- `system`
  OSAL 内核层，负责任务调度、内存管理、中断抽象、系统时基、软件定时器、队列、事件、互斥量。

- `platform`
  适配层模板和板级实例。
  `osal_platform_cortexm.*` 是填写模板，不参与当前工程编译。
  `platform/example/stm32f4/*` 是当前 STM32F407ZGT6 的适配示例。

- `components`
  OSAL 之上的可复用小组件，目前包含 `USART`、`Flash`、`RTT`、`KEY`。

## 当前能力

- 协作式任务调度
- 高 / 中 / 低三档优先级
- 统一静态堆 `osal_mem`
- 微秒忙等待、毫秒节拍和运行时间获取
- 单次 / 周期软件定时器
- 泛型固定成员大小消息队列
- 事件与互斥量
- ISR 上下文判断和关中断保护
- 基于桥接模式的 USART / Flash 组件

## 当前核心行为

- `OSAL_PLATFORM_TICK_RATE_HZ` 决定 `SysTick` 中断周期。
- `osal_init()` 会自动完成：
  - 中断分组配置
  - `SysTick` 优先级配置
  - `SysTick` 周期、使能和中断开关配置
- 默认配置对齐常见 `FreeRTOS Cortex-M` 风格：
  - 分组：`Group 4`
  - `SysTick`：最低优先级
- `osal_timer_delay_us()` 采用“读当前计数器 + 忙等待”。
- `osal_task_sleep()` / `osal_task_sleep_until()` 都是任务挂起语义，不会像 `HAL_Delay()` 那样直接卡死整个框架。
- 软件定时器采用“最近到期时间”优化，不会每次 tick 都全表扫描。
- 队列已经支持事件驱动等待 / 唤醒：
  - `0` 表示不等待
  - `N ms` 表示超时等待
  - `OSAL_WAIT_FOREVER` 表示永久等待

## 移植时用户需要做什么

### 1. 先改 `system/Inc/osal_platform.h`

用户最先要填写的是这几个宏：

- `OSAL_PLATFORM_CPU_CLOCK_HZ`
- `OSAL_PLATFORM_SYSTICK_CLOCK_HZ`
- `OSAL_PLATFORM_TICK_RATE_HZ`
- `OSAL_PLATFORM_CONFIGURE_PRIORITY_GROUP`
- `OSAL_PLATFORM_NVIC_PRIORITY_GROUP_RAW`
- `OSAL_PLATFORM_CONFIGURE_SYSTICK_PRIORITY`
- `OSAL_PLATFORM_NVIC_PRIO_BITS`
- `OSAL_PLATFORM_SYSTICK_PRIORITY_LEVEL`

如果目标芯片也是标准 `Cortex-M`，通常只改时钟和 tick 频率就够了。

### 2. 再写板级适配文件

参考：

- `platform/osal_platform_cortexm.h`
- `platform/osal_platform_cortexm.c`
- `platform/example/stm32f4/osal_platform_stm32f4.h`
- `platform/example/stm32f4/osal_platform_stm32f4.c`

通常只需要桥接：

- UART 单字节发送
- Flash 解锁 / 擦除 / 写入
- LED 示例钩子

### 3. 应用层最少接入方式

`main.c` 里通常只需要：

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

系统时基中断里调用：

```c
void SysTick_Handler(void)
{
    osal_tick_handler();
}
```

## 示例代码在哪里

功能示例统一放在：

- `platform/example/stm32f4/osal_integration_stm32f4.c`

这里面包含了：

- 任务创建与启动
- 事件
- 互斥量
- 队列
- 软件定时器
- USART 组件
- Flash 组件

目标就是尽量让你把对应片段直接复制到 `main.c` 或 `app.c` 里就能跑。

## 调试日志

- 系统层统一只走 `OSAL_DEBUG_HOOK(module, message)`。
- 默认不开启 debug，也不会默认绑定 `printf`。
- 只有你自己打开 `OSAL_CFG_ENABLE_DEBUG` 并定义 `OSAL_DEBUG_HOOK` 后，系统层才会输出诊断。
- 输出后端由你自己决定，可以接 `RTT`，也可以接 `USART`。

## 资源契约

- `create / alloc` 成功后，资源所有权归调用方。
- `delete / destroy / free` 成功后，句柄立即失效。
- `delete(NULL)` 一类接口默认按安全空操作处理。
- release 构建优先保持轻量。
- debug 构建下，只要实现层能检测到重复释放、非法句柄、错误上下文调用，就会通过 `OSAL_DEBUG_HOOK` 诊断。

## 说明文件为什么现在只保留两份

- `README.md`
  放总览、移植入口、核心行为、使用入口。

- `CHANGELOG.md`
  只记录重要结构调整和接口变更。

其余原先拆出去的 `PORTING_GUIDE / USAGE_EXAMPLES / API_CAPABILITY_TABLE / components README` 已经合并进本文件，避免文档过散。
