# task：协作式任务调度

## 1. 模块职责

`task` 当前只负责协作式调度本身：

- 创建和删除任务对象
- 启动和停止任务
- 管理按优先级划分的任务链表
- 执行一轮调度
- 提供一次同步 `yield`

它不再负责任何“等待后恢复”的抽象。

## 2. 任务对象结构

当前实现中的任务控制块只保留这些字段：

```c
struct osal_task {
    osal_task_fn_t fn;
    void *arg;
    osal_task_state_t state;
    osal_task_priority_t priority;
    struct osal_task *next;
};
```

字段含义：

- `fn`
  任务入口函数
- `arg`
  传给任务函数的用户参数
- `state`
  当前调度状态
- `priority`
  所属优先级链表
- `next`
  同优先级链表链接指针

## 3. 状态模型

`osal_task_state_t` 只保留三种状态：

- `OSAL_TASK_READY`
- `OSAL_TASK_RUNNING`
- `OSAL_TASK_SUSPENDED`

状态切换很简单：

- `create` 后初始为 `SUSPENDED`
- `start` 把任务切到 `READY`
- 调度器执行任务前切到 `RUNNING`
- 任务函数返回后，如果仍是 `RUNNING`，调度器把它改回 `READY`
- `stop` 把任务切到 `SUSPENDED`

## 4. 调度链表

### 4.1 全局调度表

当前调度器维护三条优先级链表：

```c
static osal_task_t *s_task_lists[OSAL_TASK_PRIORITY_COUNT];
```

优先级从高到低依次为：

- `OSAL_TASK_PRIORITY_HIGH`
- `OSAL_TASK_PRIORITY_MEDIUM`
- `OSAL_TASK_PRIORITY_LOW`

### 4.2 链表操作

内部链表相关函数包括：

- `osal_task_contains()`
  检查句柄是否仍在调度器管理范围内
- `osal_task_list_append()`
  追加到指定优先级链表尾部
- `osal_task_list_remove()`
  从指定优先级链表摘除

## 5. 调度入口与内部流程

### 5.1 `osal_run_priority_list()`

这个函数负责按链表顺序扫描某个优先级列表：

1. 跳过本轮指定的 `skip_task`
2. 只执行 `READY` 任务
3. 调用任务函数前切到 `RUNNING`
4. 任务返回后，若任务仍在调度器内且状态仍为 `RUNNING`，再切回 `READY`

### 5.2 `osal_run_internal()`

这是调度器的核心一轮逻辑：

1. 增加 `s_scheduler_depth`
2. 每轮都先跑高优先级
3. 每轮都再跑中优先级
4. 低优先级在两种情况下会被扫描：
   - 本轮高/中优先级都没跑到任务
   - `s_low_scan_count` 达到 `OSAL_TASK_LOW_SCAN_PERIOD`
5. 结束后减少 `s_scheduler_depth`

这样做的目的，是在保证高/中优先级响应性的同时，给低优先级任务保留最小公平性。

### 5.3 `osal_run()`

`osal_run()` 是主循环层入口：

- 先调用 `osal_timer_poll()`
- 检查当前是否允许进入新一轮调度
- 再调用 `osal_run_internal(NULL)`

## 6. `yield` 的真实语义

`osal_task_yield()` 的语义是：

1. 先调用 `osal_timer_poll()`
2. 在当前任务调用栈里同步触发一次嵌套调度
3. 本轮嵌套调度跳过当前任务自己
4. 嵌套调度返回后，当前任务继续向下执行

因此它更接近“主动让出一次执行机会”，而不是“把自己挂起等待恢复”。

## 7. 生命周期接口

### 7.1 `osal_task_create()`

- 只允许在任务态调用
- 从统一静态堆里分配任务控制块
- 初始状态为 `SUSPENDED`
- 自动挂到对应优先级链表

### 7.2 `osal_task_start()`

- 只允许在任务态调用
- 把任务状态改成 `READY`

### 7.3 `osal_task_stop()`

- 只允许在任务态调用
- 把任务状态改成 `SUSPENDED`

### 7.4 `osal_task_delete()`

- 只允许在任务态调用
- 只允许删除当前不在执行中的任务
- 成功后从优先级链表摘除并释放控制块

这里有一个必须明确的边界：

- 运行中的任务不能在当前执行轮次里直接 `delete`
- 因为当前 OSAL 没有独立任务栈，删除正在执行的任务会破坏当前调用链

## 8. 延时与周期任务应该怎么写

`task` 模块已经不提供专门的延时接口。

如果你想实现“每 500ms 执行一次”的周期任务，推荐做法是：

1. 在任务私有上下文里保存 `interval_ms / next_run_ms / initialized`
2. 每轮调用时用当前 tick 判断是否到期
3. 到期后执行一次动作
4. 再把 `next_run_ms` 推到下一次节拍

典型模式如下：

```c
typedef struct {
    uint32_t interval_ms;
    uint32_t next_run_ms;
    bool initialized;
} periodic_ctx_t;

static bool tick_reached(uint32_t now_ms, uint32_t deadline_ms) {
    return ((int32_t)(now_ms - deadline_ms) >= 0);
}
```

这种写法与当前 OSAL 的真实执行模型完全一致。

## 9. 使用边界

- 任务函数应尽量短小，做完一小段工作就返回
- 不要在任务里写长时间死循环等待资源
- 如果业务依赖跨多轮推进，应自己写状态机
- 如果确实需要同步等待资源，应优先考虑这个等待是否来自异步硬件路径
