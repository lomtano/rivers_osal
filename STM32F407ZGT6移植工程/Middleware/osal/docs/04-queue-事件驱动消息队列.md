# queue：事件驱动消息队列

## 1. 模块职责

`osal_queue` 提供：

- 固定项大小消息队列
- 非阻塞发送/接收
- 超时等待
- 永久等待
- 事件驱动唤醒等待任务

对应文件：

- [osal_queue.h](/abs/path/A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal/system/Inc/osal_queue.h)
- [osal_queue.c](/abs/path/A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal/system/Src/osal_queue.c)

## 2. 队列模型

当前队列不是变长消息，而是“固定项大小环形队列”。

也就是说：

- 每个元素大小在创建时确定
- 后续每次 `send/recv` 都按固定 `item_size` 拷贝

这适合：

- 结构体消息
- 指针
- 固定长度数组

不适合：

- 真正变长的消息体

## 3. 核心数据结构

一个队列对象内部通常包含：

- `storage`
- `length`
- `item_size`
- `head`
- `tail`
- `count`
- `owns_storage`
- `wait_send_list`
- `wait_recv_list`

可以理解为两部分：

1. 环形缓冲区状态
2. 等待任务链表

## 4. 环形缓冲区怎么工作

发送时：

- 把消息写到 `tail`
- `tail = (tail + 1) % length`
- `count++`

接收时：

- 从 `head` 读消息
- `head = (head + 1) % length`
- `count--`

这是典型环形队列。

优点是：

- 不需要搬移数据
- 固定长度项处理简单
- 裸机上很适合做消息缓冲

## 5. 等待语义

当前队列支持三种等待模式：

- `0`
  - 不等
  - 当前不能发送/接收就立刻返回
- `N ms`
  - 最多等待 N 毫秒
- `OSAL_WAIT_FOREVER`
  - 一直等

这套语义和 FreeRTOS 风格是接近的。

## 6. 阻塞等待时到底发生了什么

如果任务调用 `recv_timeout(..., OSAL_WAIT_FOREVER)`，且队列为空：

1. 当前任务不会继续轮询
2. 当前任务被标记为 `BLOCKED`
3. 被挂到这个队列的 `wait_recv_list`
4. 调度器后续普通扫描时会跳过这个任务

所以这个等待不会阻塞整个系统，只会阻塞当前任务。

发送等待也是同理，只是挂到 `wait_send_list`。

## 7. 为什么说它是事件驱动的

关键不在“它能等待”，而在“队列状态变化时会主动唤醒任务”。

例如：

- 某任务在等接收
- 另一个任务成功 `send`

这时队列实现不会只是把消息塞进去结束，而是会继续：

- 从 `wait_recv_list` 中挑一个最合适的等待任务
- 直接把它置为 `READY`

所以这是事件驱动，不是简单的轮询 + yield。

## 8. 唤醒策略是什么

当前等待唤醒策略是：

- 高优先级优先
- 同优先级保持先入先出

这意味着：

- 更关键的任务恢复更快
- 同级任务恢复顺序可预测

## 9. 为什么 `queue` 比 `event/mutex` 更成熟

因为队列已经具备：

- 等待链表
- 阻塞状态管理
- 事件触发后的主动唤醒
- 超时恢复

而 `event/mutex` 目前更多还是：

- 条件不满足就 `yield`
- 没有真正的等待队列和事件驱动恢复

所以当前系统里，`queue` 是最完整的同步原语。

## 10. `send_from_isr / recv_from_isr` 的意义

这组接口的目标是：

- 在 ISR 上下文下也能安全地改变队列状态
- 并在改变成功后通知等待任务恢复

它仍然不是抢占式 RTOS 的“立即切换任务”，但已经实现了：

- ISR 改变队列状态
- 对应等待任务变成 READY

后续主循环再运行时，任务就能尽快继续执行。

## 11. 这个设计的收益

相对于“任务里循环试探队列是否有消息”，这套设计的收益是：

- CPU 空转更少
- 高优先级等待任务恢复更及时
- 系统负载更稳定
- 等待逻辑更接近 RTOS 使用体验

## 12. 边界和限制

当前队列模型的边界：

- 固定项大小，不支持真变长消息
- 唤醒是协作式恢复，不是抢占式立即切换
- 队列对象删除时，等待中的任务要正确处理恢复语义

## 13. 看源码时建议重点关注

建议重点看：

- 队列对象字段定义
- `enqueue / dequeue`
- `wait_send_list / wait_recv_list`
- `prepare_wait`
- `wake_one_waiter`
- `send_timeout / recv_timeout`

这些部分基本覆盖了当前 queue 的全部行为。
