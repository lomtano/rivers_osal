# queue：环形消息队列与同步重试

> 注：当前实现应理解为“环形队列 + 同步重试”，而不是“等待后自动唤醒”的事件驱动队列。

## 1. 模块职责

当前 `queue` 是一个固定项大小的环形消息队列。
它负责：

- 创建和删除队列
- 查询当前消息数量
- 任务态同步发送与接收
- ISR 态立即尝试发送与接收

你可以把它直接理解成：

- 一个使用 `osal_mem` 动态分配底层存储的 ring buffer
- 外面再包了一层任务态 `timeout_ms` 同步重试接口

它不负责任务挂起、等待链表和自动唤醒。

## 2. 队列对象结构

当前实现保留这些核心字段：

```c
struct osal_queue {
    uint8_t *storage;
    uint32_t head;
    uint32_t tail;
    uint32_t length;
    uint32_t item_size;
    uint32_t count;
    struct osal_queue *next;
};
```

字段含义：

- `storage`
  队列底层数据区
- `head`
  读取位置
- `tail`
  写入位置
- `length`
  队列容量，单位是“项”
- `item_size`
  每项固定字节数
- `count`
  当前已存项数
- `next`
  活动队列链表指针，主要用于句柄校验

## 3. 内存来源

当前队列的控制块和数据区都统一来自 `osal_mem_alloc()`。
因此公开创建接口只保留：

- `osal_queue_create(length, item_size)`

不再提供“用户自带静态数据缓冲区”的创建方式。

## 4. 对外接口

### 4.1 任务态接口

- `osal_queue_create()`
- `osal_queue_delete()`
- `osal_queue_get_count()`
- `osal_queue_send(q, item, timeout_ms)`
- `osal_queue_recv(q, item, timeout_ms)`

### 4.2 ISR 接口

- `osal_queue_send_from_isr(q, item)`
- `osal_queue_recv_from_isr(q, item)`

ISR 版本始终是立即尝试，不接受 `timeout_ms`。

## 5. 当前 queue 不是“等待后唤醒”模型

这是当前实现最容易被误解的点。

现在的 queue `不是` 下面这种模型：

- 任务调用 `recv()` 后进入 queue 等待队列
- 另一个任务或 ISR 调用 `send()` 成功后
- queue 把“等待这个队列的任务”切回 `READY`
- 调度器随后从任务上次阻塞的位置继续执行

当前实现没有这些东西：

- 没有 queue wait list
- 没有 task waiting state
- 没有 send/recv 后的 ready 唤醒逻辑
- 没有任务上下文保存/恢复

结合当前 `task` 模型，这意味着：

- OSAL 只有 `READY / RUNNING / SUSPENDED`
- 没有“阻塞在某个 queue 上”的任务状态
- 所以系统无法实现“恢复后从当时等队列的那一行继续往下跑”

## 6. `timeout_ms` 的真实语义

### 6.1 `timeout_ms = 0U`

只尝试一次：

- 发送时队列满，返回 `OSAL_ERR_RESOURCE`
- 接收时队列空，返回 `OSAL_ERR_RESOURCE`

### 6.2 `timeout_ms = N`

表示在 `N ms` 时间窗口内反复尝试：

- 成功时返回 `OSAL_OK`
- 到窗口末尾仍失败时返回 `OSAL_ERR_TIMEOUT`

内部模型可以理解为：

```c
start = osal_timer_get_tick();
do {
    status = try_send_or_recv();
    if (status == OSAL_OK) {
        return OSAL_OK;
    }
} while ((osal_timer_get_tick() - start) < timeout_ms);

return OSAL_ERR_TIMEOUT;
```

这仍然发生在当前调用栈里。
它不是挂起等待，而是同步忙等重试。

## 7. “事件驱动”现在体现在哪里

当前文档里说 queue 带一点“事件驱动”特征，指的不是任务唤醒，而是这件事：

- 在你重试的这段时间里
- 队列状态可能被异步路径改掉
- 所以下一次重试可能突然成功

这些异步路径通常包括：

- ISR
- DMA 完成中断
- 外设硬件事件

所以现在的“event-driven”更准确地说是：

- 队列状态可以被异步事件推进
- 但任务侧等待消息的方式依然是自己下一次再检查

## 8. 任务态接口和 ISR 接口的区别

### 8.1 `osal_queue_send()` / `osal_queue_recv()`

- 设计给任务态使用
- 如果在 ISR 里调用，会返回 `OSAL_ERR_ISR`
- 支持 `timeout_ms`
- `timeout_ms > 0` 时本质上是同步忙等重试

### 8.2 `osal_queue_send_from_isr()` / `osal_queue_recv_from_isr()`

- 设计给 ISR 里的即时单次操作使用
- 不接受 `timeout_ms`
- 只做一次 enqueue/dequeue 尝试
- 成功返回 `OSAL_OK`
- 资源不满足返回 `OSAL_ERR_RESOURCE`

可以把 ISR 版本理解成：

- 只有“立刻试一次”
- 没有“等一会儿再试”

## 9. 适用场景

### 9.1 适合

- ISR 向任务侧投递固定大小消息
- DMA / 外设完成中断向 OSAL 顶层循环或协作任务侧投递事件
- 想用固定单元 ring buffer 管理消息，而不想手写 head/tail/count

### 9.2 不适合

如果你的目标是：

- 发完消息自动唤醒等待该队列的任务
- 消费者任务平时完全不需要自己安排检查时机
- 任务恢复后直接从“当时阻塞的那一行”继续跑

那当前 queue 不适合。
这种模型需要真正的等待链表、任务等待状态和上下文恢复语义，当前 OSAL 没做这层抽象。

## 10. 推荐使用方式

如果你接受当前 queue 就是 ring buffer + 同步重试封装，那么推荐这样理解和使用：

- 把 queue 当成“固定单元、动态分配底层存储的环形缓冲区”
- `send_from_isr/recv_from_isr` 负责异步即时投递/取走
- 任务侧根据自己的状态机、节拍或显式调用时机决定何时检查队列
- 如果资源状态主要靠异步硬件推进，可以使用 `timeout_ms > 0`
- 如果资源状态主要靠别的协作任务推进，优先使用 `timeout_ms = 0U`

## 11. 关键内部函数

- `osal_queue_storage_size()`
  检查 `length * item_size` 是否有效并计算总字节数
- `osal_queue_link()` / `osal_queue_unlink()`
  维护活动队列链表
- `osal_queue_enqueue_locked()`
  在关中断保护下写入一项
- `osal_queue_dequeue_locked()`
  在关中断保护下取出一项
- `osal_queue_try_send()` / `osal_queue_try_recv()`
  一次立即尝试的任务态封装

## 12. 使用边界

- 当前队列是固定项大小队列，不是可变长消息队列
- 当前队列不提供任务等待队列
- 当前队列不提供发送后自动唤醒等待任务
- 当前队列不提供“恢复到之前阻塞位置继续执行”的语义
- 如果业务依赖跨多轮协作推进资源状态，应在任务层实现状态机，而不是把 `queue(timeout_ms)` 当成任务调度原语
