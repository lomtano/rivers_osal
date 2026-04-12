# OSAL 说明

这套 `OSAL` 面向 Cortex-M 裸机工程，目标是用尽量少的代码，把任务调度、统一时基、消息队列、软件定时器和基础同步能力搭起来，同时把和具体 MCU SDK 的耦合尽量压缩到 `platform` 层。

## 目录结构

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
|-- docs/
`-- CHANGELOG.md
```

## 分层职责

- `system`
  OSAL 内核层。负责任务调度、统一时基、软件定时器、消息队列、内存管理、中断抽象、事件和互斥量。
- `platform`
  架构和板级适配层。
  `osal_platform_cortexm.*` 是模板说明文件，不参与当前工程编译。
  `platform/example/stm32f4/*` 是当前 STM32F407ZGT6 示例工程的具体适配文件。
- `components`
  挂在 OSAL 之上的外围组件。
  当前包含 `USART`、`Flash`、`RTT`、`KEY`。

## 当前核心行为

- `OSAL_PLATFORM_TICK_RATE_HZ` 决定 `SysTick` 中断周期。
- `osal_init()` 会自动完成：
  - 中断分组配置
  - `SysTick` 优先级配置
  - `SysTick` 周期、使能和中断开关配置
- 默认配置风格与常见 `FreeRTOS Cortex-M` 端口接近：
  - 分组：`Group 4`
  - `SysTick`：最低优先级
- `osal_timer_delay_us()` / `osal_timer_delay_ms()` 是忙等延时。
- `osal_task_sleep()` / `osal_task_sleep_until()` 是任务挂起，不会阻塞整个系统。
- 软件定时器使用“最近到期时间”优化，不会每次 tick 都全表扫描。
- 队列已支持事件驱动等待与唤醒：
  - `0U` 表示不等待
  - `N` 表示最多等待 `N ms`
  - `OSAL_WAIT_FOREVER` 表示永久等待
- `event` 和 `mutex` 现在也已统一到“等待链表 + BLOCKED + 事件驱动唤醒”模型。
- `OSAL_ERR_RESOURCE / OSAL_ERR_BLOCKED / OSAL_ERR_DELETED` 的语义已经拆开：
  - `OSAL_ERR_RESOURCE`：资源当前不可用，且本次没有进入等待
  - `OSAL_ERR_BLOCKED`：当前任务已经进入等待链表
  - `OSAL_ERR_DELETED`：等待对象在等待期间被删除

## 最小移植步骤

1. 把 `osal/system/Inc` 和 `osal/system/Src` 加入工程。
2. 在 `osal/system/Inc/osal_platform.h` 中配置：
   - `OSAL_PLATFORM_CPU_CLOCK_HZ`
   - 如有需要，再改 `OSAL_PLATFORM_SYSTICK_CLOCK_HZ`
   - 如有需要，再改 `OSAL_PLATFORM_TICK_RATE_HZ`
3. 应用层包含 `osal.h`。
4. 初始化完成后调用 `osal_init()`。
5. 主循环中持续调用 `osal_run()`。
6. 在 `SysTick_Handler()` 中调用 `osal_tick_handler()`。

## 用户通常不需要手动处理的事情

- `SysTick` 时钟源和周期配置
- `SysTick` 使能与中断开关
- 常规 `tick/uptime` 回绕处理
- 任务 `sleep/sleep_until` 的阻塞与恢复
- 队列的等待链表、唤醒链路和超时计算
- 事件与互斥量的等待链表、唤醒链路和超时计算
- 软件定时器的下一次到期时间维护

## 用户仍然需要注意的边界

- 这套调度器是协作式，不是抢占式。
- 任务函数应尽量短小，做完一小段工作就 return。
- `delay_us()` / `delay_ms()` 仍然会占用 CPU。
- `mutex` 目前仍是最小实现：没有 owner 检查，也没有优先级继承。

## 推荐阅读顺序

1. `docs/01-总览与移植边界.md`
2. `docs/02-task-任务调度与延时.md`
3. `docs/03-timer-时基与软件定时器.md`
4. `docs/04-queue-事件驱动消息队列.md`
5. `docs/05-mem-统一堆与内存池.md`
6. `docs/06-irq-event-mutex-platform.md`
7. `docs/07-components-外围组件与示例.md`

## 示例入口

- 当前可直接运行的工程示例在：
  - `STM32F407ZGT6移植工程/Core/Src/main.c`
- 可复制到自己工程的功能示例在：
  - `platform/example/stm32f4/osal_integration_stm32f4.c`

