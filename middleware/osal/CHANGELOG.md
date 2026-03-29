# Changelog

## 2026-03-29

### 目录重组

- 将系统层重组为 `middleware/osal/system/Inc` 和 `middleware/osal/system/Src`
- 将组件层收拢到 `middleware/osal/components/`
- 保留平台示例在 `middleware/osal/examples/`
- 将移植文档和示例文档统一放到 `middleware/osal/` 根目录

### 接口调整

- 删除 `osal_status.h`，将 `osal_status_t` 集中到 `osal.h`
- 新增显式开中断接口 `osal_irq_enable()`
- 简化时间基准接口，保留 `osal_timer_inc_tick()` 作为 1us 计数入口

### 架构调整

- 统一使用 `osal_mem` 管理 OSAL 控制对象和组件对象分配
- 组件层采用桥接模式，支持 UART 和 Flash 两类抽象组件
- STM32F4 示例中将平台适配代码与使用示例代码分离

### 示例与文档

- 增加两个任务无阻塞点灯示例
- 增加队列生产者/消费者示例
- 增加单次和周期性软件定时器打印示例
- 增加 Flash 组件桥接与使用示例
- 重写 README、移植指南和使用示例文档
