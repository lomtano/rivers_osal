# OSAL Middleware

这是一个面向裸机 32 位 MCU 的轻量化 OSAL 工程骨架，目标是像小型 FreeRTOS 一样易移植，但保持更简单的协作式调度模型和更低的接入成本。

当前这套 `middleware/osal` 已经按三层结构整理完成：

- `system/`
  OSAL 系统层，包含任务、队列、事件、互斥量、内存管理、时间基准、中断抽象。
- `components/`
  组件层，放可复用的小组件，例如 USART、Flash，后续也可以扩展 RTT、Bootloader 等。
- `examples/`
  示例层，放具体平台适配和使用案例，例如 `stm32f4`。

## 目录结构

```text
middleware/osal/
|-- system/
|   |-- Inc/
|   `-- Src/
|-- components/
|   |-- flash/
|   |   |-- Inc/
|   |   `-- Src/
|   `-- usart/
|       |-- Inc/
|       `-- Src/
|-- examples/
|   `-- stm32f4/
|-- README.md
|-- PORTING_GUIDE.md
|-- USAGE_EXAMPLES.md
`-- CHANGELOG.md
```

## 当前能力

- 协作式任务调度
- 队列生产者/消费者
- 事件与互斥量
- 统一静态堆 `osal_mem`
- 1us 计数入口 `osal_timer_inc_tick()`
- HAL 风格 `osal_timer_get_tick()`
- 单次与周期性软件定时器
- 中断开关与 ISR 上下文判断
- USART 单字节桥接组件
- Flash 解锁/上锁/擦除/多写宽桥接组件

## 接入原则

1. OSAL 核心不直接依赖具体 MCU SDK。
2. 与芯片相关的东西都放进 `examples/<platform>/` 或你自己的板级适配目录。
3. 组件层通过桥接模式复用，不把平台细节泄露到上层逻辑。

## 最常用的接入点

- 中断接口：`osal_irq_disable()`、`osal_irq_enable()`、`osal_irq_restore()`、`osal_irq_is_in_isr()`
- 时间接口：`osal_timer_inc_tick()` 或 `osal_timer_set_us_provider()`
- 主循环：`osal_run()`
- 串口重定向：`periph_uart_fputc()`

## 文档

- 移植步骤见 `middleware/osal/PORTING_GUIDE.md`
- 使用示例见 `middleware/osal/USAGE_EXAMPLES.md`
- 变更记录见 `middleware/osal/CHANGELOG.md`
