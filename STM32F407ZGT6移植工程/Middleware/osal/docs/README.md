# OSAL 说明文档

本目录只解释当前实现，不保留已经删除的旧行为模型。

建议阅读顺序：

1. `01-总览与移植边界.md`
2. `02-task-任务调度与延时.md`
3. `03-timer-时基与软件定时器.md`
4. `04-queue-事件驱动消息队列.md`
   当前内容是“环形队列 + 同步重试”语义，文件名为历史保留。
5. `05-mem-统一堆与内存池.md`
6. `06-irq-event-mutex-platform.md`
   当前内容是 `irq / cortexm / platform-example` 边界，文件名为历史保留。
7. `07-components-外围组件与示例.md`

阅读目标：

- 先理解 `system / platform / components` 的职责边界。
- 再掌握 `task / timer / queue / mem / irq` 这几个核心模块的真实语义。
- 最后按需查看 `USART / Flash / RTT / KEY` 等外围组件与示例。
