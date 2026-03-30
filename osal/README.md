# OSAL 中间件

`middleware/osal` 是一套面向 32 位 MCU 的轻量裸机 OSAL 骨架。
它希望具备接近 FreeRTOS 的可移植性，但保持更小的代码规模、更薄的平台适配面，
并优先服务于“快速搭建任务逻辑框架”这件事。

## 目录结构

```text
middleware/osal/
|-- system/
|   |-- Inc/
|   `-- Src/
|-- components/
|   |-- periph/
|   |   |-- usart/
|   |   `-- flash/
|   `-- README.md
|-- examples/
|   `-- stm32f4/
|-- README.md
|-- PORTING_GUIDE.md
|-- USAGE_EXAMPLES.md
`-- CHANGELOG.md
```

## 分层说明

- `system/`
  OSAL 系统层，提供任务、队列、事件、互斥量、内存管理、中断抽象和定时器。
- `components/`
  可复用的小组件层。目前放的是 `periph/`，后续也可以继续扩展 `rtt/`、
  `bootloader/` 等独立组件。
- `components/periph/`
  外设桥接组件层，例如 `usart/` 和 `flash/`。
- `examples/`
  平台适配示例和集成演示代码。

## 当前能力

- 协作式任务调度
- 泛型固定成员大小消息队列
- 事件与互斥量
- `osal_mem` 统一静态内存管理
- 基于 `1us` 中断源推导出的 HAL 风格毫秒 `tick`
- 微秒级忙等待延时
- 单次和周期性软件定时器
- 中断开关与 ISR 上下文判断钩子
- 基于“单字节发送回调”的 `USART` 桥接组件
- 支持擦除、读取和多种写入宽度的 `Flash` 桥接组件

## 定时模型

定时器设计刻意保持简单：

- 平台层准备一个固定 `1us` 周期的中断源。
- 中断里只调用一次 `osal_timer_inc_tick()`。
- OSAL 内部自动维护：
  - `osal_timer_get_uptime_us()`
  - `osal_timer_get_uptime_ms()`
  - `osal_timer_get_tick()`
  - `osal_timer_delay_us()`
  - 软件定时器超时判定与回绕处理

不再需要额外注册时间提供函数。

## 主循环

```c
while (1) {
    osal_run();
}
```

`osal_run()` 内部已经会轮询软件定时器，因此应用主循环可以保持很干净。

## 内存说明

`osal_queue_create()`、`osal_task_create()`、`osal_timer_create()` 等接口使用的都不是
系统 `heap`，而是 `osal_mem` 管理的 OSAL 静态大数组。

如果你希望进一步显式控制内存来源，可以：

- 在启动早期调用 `osal_mem_init()`，传入你自己的静态缓冲区
- 对队列使用 `osal_queue_create_static()`，让消息缓存区完全由用户提供

因此即使需要支持结构体、指针、定长数组等任意固定大小消息类型，也不必依赖 MCU 工程里的
系统堆。

## 文档

- 移植步骤：`middleware/osal/PORTING_GUIDE.md`
- 使用示例：`middleware/osal/USAGE_EXAMPLES.md`
- 更新日志：`middleware/osal/CHANGELOG.md`
