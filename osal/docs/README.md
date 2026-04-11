# OSAL 说明文档

本目录用于集中存放 `osal` 的模块说明、设计边界和实现原理文档。

建议阅读顺序：

1. `01-总览与移植边界.md`
2. `02-task-任务调度与延时.md`
3. `03-timer-时基与软件定时器.md`
4. `04-queue-事件驱动消息队列.md`
5. `05-mem-统一堆与内存池.md`
6. `06-irq-event-mutex-platform.md`
7. `07-components-外围组件与示例.md`

阅读目标：

- 先搞清楚 `system / platform / components` 的职责边界
- 再理解 `task / timer / queue / mem` 这四个核心模块
- 最后再看 `irq / event / mutex / usart / flash / RTT / KEY` 这些支撑层和组件

当前文档只解释现有实现，不修改任何源码语义。
