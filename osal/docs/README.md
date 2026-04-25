# OSAL 文档索引

本目录中的编号文档解释当前实现，不保留已经删除的旧行为模型。

`plans/`、`specs/`、`progress/` 是历史设计和迁移记录，可能包含已经删除的 `event/mutex`、旧阻塞语义或中间方案；它们只用于追溯设计过程，不作为当前 API 使用说明。

建议阅读顺序：
1. `01-总览与移植步骤.md`
2. `02-task-协作式任务调度.md`
3. `03-timer-系统时基-软件定时器与延时.md`
4. `04-queue-环形消息队列.md`
5. `05-mem-静态堆与内存池.md`
6. `06-irq-中断控制抽象.md`
7. `07-cortexm-内核外设配置.md`
8. `08-components-外围组件与板级示例.md`

阅读目标：
- 先理解 `system / cortexm / platform/example / components` 的职责边界。
- 再掌握 `task / timer / queue / mem / irq` 这几个核心模块的真实语义。
- 最后按需查看 `USART / Flash / RTT / KEY` 等外围组件与板级示例。
