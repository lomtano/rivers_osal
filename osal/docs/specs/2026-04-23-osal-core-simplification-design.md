# OSAL 核心收缩与接口重构设计

日期：2026-04-23

## 1. 目标

这次改造的目标不是继续给当前 OSAL 增加“像 RTOS 一样的阻塞语义”，而是把它明确收口成一套更符合当前裸机场景、边界更干净、文档与实现一致的协作式内核。

本次设计以“修改后的 OSAL 为唯一事实来源”，不保留旧的历史语义包袱。核心目标如下：

1. 删除已经不再需要的 `event`、`mutex` 模块。
2. 新增统一配置头 `osal_config.h`，把功能开关从 `osal.h` 中独立出来。
3. 精简 `task`，移除伪阻塞相关的 `BLOCKED / wait_* / resume_* / sleep / sleep_until` 体系，只保留纯协作式调度能力。
4. 保留 `queue`，但把接口收口成同步 `send/recv(timeout_ms)` 模型。
5. 明确 `queue(timeout_ms)` 的真实语义：它是“基于系统 tick 的同步超时重试”，不是任务挂起，也不是上下文恢复。
6. 保留 `timer` 现有能力不改动，同时把 tick 防溢出机制的实现方法和原理补进文档。
7. 把 DWT 原始后端能力下沉到 `platform`，把 profiling 改成独立可选功能。
8. 重写 `docs` 下的核心文档，使模块说明、函数说明、状态码说明与新实现完全一致。

## 2. 设计前提与非目标

### 2.1 设计前提

这次设计明确接受以下现实前提：

1. 当前 OSAL 没有独立任务栈，也没有 CPU 上下文保存/恢复。
2. 当前 OSAL 的任务函数本质上只是“被调度器重复调用的普通 C 函数”。
3. 因此，任何“像线程一样在阻塞点下一条语句继续执行”的语义都不成立。
4. `osal_task_yield()` 可以保留，因为它是一次同步的嵌套调度调用；它不是“挂起后恢复”。
5. `queue(timeout_ms)` 如果不调用 `yield`，那它的等待本质上就是忙等；这次设计接受这一点，并把它写进接口契约。

### 2.2 非目标

本次不处理以下方向：

1. 不把当前 OSAL 改造成抢占式 RTOS。
2. 不引入任务上下文保存、独立栈、协程 continuation 或 longjmp 风格恢复。
3. 不修改 `timer` 的功能形态。
4. 不把 `queue` 改成可变长消息队列。
5. 不保留“看起来像阻塞挂起，实际上并不成立”的旧接口与旧状态码。

## 3. 模块边界

### 3.1 保留模块

本次保留以下核心模块：

- `task`
- `timer`
- `irq`
- `mem`
- `platform`
- `queue`

### 3.2 删除模块

本次直接删除以下模块的公开接口、源码和对应文档语义：

- `event`
- `mutex`

### 3.3 新增模块

本次新增：

- `Middleware/osal/system/Inc/osal_config.h`

它作为 OSAL 的统一配置入口，负责放置所有功能开关和构建配置。

## 4. `osal_config.h` 设计

### 4.1 文件位置与包含关系

新配置头固定放在：

- `Middleware/osal/system/Inc/osal_config.h`

包含关系调整为：

1. `osal.h` 先包含 `osal_config.h`，再声明状态码和聚合其他模块头文件。
2. 其他模块头文件与源码如果需要配置宏，也统一通过 `osal_config.h` 获取，而不是各自依赖 `osal.h` 里的默认定义。

### 4.2 配置职责

`osal_config.h` 负责承载这类配置：

- `OSAL_CFG_ENABLE_DEBUG`
- `OSAL_CFG_ENABLE_QUEUE`
- `OSAL_CFG_ENABLE_IRQ_PROFILE`
- 现有外围组件开关，例如 `USART/FLASH`

原则是：

1. 功能开关统一放一处。
2. `osal.h` 不再承担“功能配置中心”的职责。
3. `OSAL_CFG_ENABLE_IRQ_PROFILE` 独立存在，不依附于 `OSAL_CFG_ENABLE_DEBUG`。

## 5. 状态码收口

### 5.1 设计原则

状态码只保留当前修改后 OSAL 真实会用到的语义，不为历史接口兼容保留空壳枚举。

### 5.2 保留状态码

公开状态码保留为：

- `OSAL_OK`
- `OSAL_ERROR`
- `OSAL_ERR_TIMEOUT`
- `OSAL_ERR_RESOURCE`
- `OSAL_ERR_PARAM`
- `OSAL_ERR_NOMEM`
- `OSAL_ERR_ISR`

### 5.3 删除状态码

以下状态码从 `osal_status_t` 和公开文档中一并删除：

- `OSAL_ERR_BLOCKED`
- `OSAL_ERR_DELETED`

原因很直接：

1. `task` 不再存在“挂起等待然后恢复”的公开语义。
2. `queue` 改成同步忙等后，也不再返回“当前任务已进入 BLOCKED”。
3. 删除 `event/mutex` 后，也不再存在“等待期间对象被删除后恢复”的这类公开路径。

## 6. `task` 模块重构

### 6.1 新定位

`task` 改完后只负责纯协作式调度，不再承担任何“阻塞等待抽象”。

它的职责收口为：

1. 管理任务对象生命周期。
2. 管理优先级调度链表。
3. 执行一轮协作式调度。
4. 提供一次同步嵌套调度能力 `yield`。

### 6.2 状态模型

`osal_task_state_t` 只保留：

- `OSAL_TASK_READY`
- `OSAL_TASK_RUNNING`
- `OSAL_TASK_SUSPENDED`

删除：

- `OSAL_TASK_BLOCKED`

### 6.3 任务结构体

`struct osal_task` 精简为只保留当前模型真正需要的字段：

- `fn`
- `arg`
- `state`
- `priority`
- `next`

删除整套与伪阻塞相关的字段：

- `periodic_wake_ms`
- `wait_start_ms`
- `wait_timeout_ms`
- `wait_deadline_ms`
- `wait_reason`
- `wait_object`
- `resume_reason`
- `resume_object`
- `resume_status`
- `periodic_sleep_initialized`
- `wait_forever`
- `resume_valid`
- `wait_next`

### 6.4 公开接口

`task` 公开接口保留：

- `osal_task_create()`
- `osal_task_delete()`
- `osal_task_start()`
- `osal_task_stop()`
- `osal_task_yield()`
- `osal_run()`

删除：

- `osal_task_sleep()`
- `osal_task_sleep_until()`

### 6.5 `yield` 语义

`osal_task_yield()` 明确解释为：

1. 在当前任务函数调用栈中，主动触发一次嵌套调度。
2. 本轮嵌套调度不立即再次执行当前任务。
3. 嵌套调度返回后，当前函数继续从 `yield()` 下一条语句往下执行。

它不是“任务挂起再恢复”，而是“当前执行流中的一次同步让出”。

### 6.6 内部函数与职责

文档中需要按修改后的实现明确列出 `task` 的内部职责，至少覆盖：

1. 任务结构体字段与用途。
2. 任务状态切换：
   - 切换为 `READY`
   - 切换为 `SUSPENDED`
3. 链表操作：
   - 追加到调度链表
   - 从调度链表中删除
4. 调度判断：
   - 检查任务是否仍在调度链表中
   - 按当前优先级链表执行一轮扫描 `osal_run_priority_list()`
5. 调度入口：
   - `osal_run_internal()`
   - `osal_run()`
6. 生命周期接口：
   - `create`
   - `delete`
   - `start`
   - `stop`
7. 同步让出：
   - `yield`

## 7. `queue` 模块重构

### 7.1 数据结构定位

`queue` 仍然是一个固定项大小的环形队列，但“固定”仅指：

1. 队列创建之后每项的大小固定不变。
2. 创建时 `length` 和 `item_size` 仍然由调用方决定。

它不是编译期写死长度或类型的静态模板队列。

### 7.2 内存来源

队列控制块和数据区统一使用 `mem` 模块管理的静态内存。

因此删除：

- `osal_queue_create_static()`

只保留：

- `osal_queue_create(length, item_size)`

### 7.3 对外接口

`queue` 对外接口收口为：

- `osal_queue_create()`
- `osal_queue_delete()`
- `osal_queue_get_count()`
- `osal_queue_send(q, item, timeout_ms)`
- `osal_queue_recv(q, item, timeout_ms)`
- `osal_queue_send_from_isr()`
- `osal_queue_recv_from_isr()`

删除旧接口：

- `osal_queue_send()` 旧的二参版本
- `osal_queue_recv()` 旧的二参版本
- `osal_queue_send_timeout()`
- `osal_queue_recv_timeout()`
- `osal_queue_create_static()`

### 7.4 `timeout_ms` 语义

`timeout_ms` 的单位明确写死为毫秒，依赖系统 tick 进行超时比较。

语义如下：

1. `timeout_ms = 0`
   - 纯非阻塞尝试一次
   - 失败立即返回 `OSAL_ERR_RESOURCE`
2. `timeout_ms = N`
   - 在 `N ms` 窗口内反复尝试
   - 成功返回 `OSAL_OK`
   - 到期仍失败返回 `OSAL_ERR_TIMEOUT`
3. 不支持 `OSAL_WAIT_FOREVER`

### 7.5 忙等语义

这是本次设计里必须明确写进接口说明的重点：

1. `queue(timeout_ms)` 是同步等待到结果再返回。
2. 它不会把当前任务切到 `BLOCKED`。
3. 它不会保存任务上下文。
4. 它不会在内部调用 `osal_task_yield()`。
5. 因此它的等待本质上是“基于系统 tick 的忙等超时重试”。

也就是说：

```c
start = get_tick();
do {
    try_send_or_recv();
    now = get_tick();
} while ((now - start) < timeout_ms);
```

它只是在循环里利用系统 tick 判断“已经等了多久”，而不是把 CPU 真正让给别的协作任务。

### 7.6 适用范围说明

文档必须明确指出这类同步等待的适用边界：

1. 如果队列状态的变化来自 ISR、DMA 完成中断或其他异步硬件路径，这种等待是有意义的。
2. 如果队列状态必须靠别的协作式任务运行后才会改变，而 `queue` 内部又不调用 `yield`，那么当前等待调用会一直占用 CPU，其他任务拿不到执行机会。
3. 因此，凡是“需要依赖其他任务推进资源状态”的场景，调用方应自己在任务层写状态机，不应指望 `queue(timeout_ms)` 代替任务挂起语义。

### 7.7 内部结构

`queue` 的环形缓冲区本体保留：

- `storage`
- `head`
- `tail`
- `length`
- `item_size`
- `count`

删除与旧阻塞语义相关的字段：

- `wait_send_list`
- `wait_recv_list`

如果实现层继续保留活动队列链表做句柄校验，那么 `next` 这类管理字段可以继续存在；但 `create_static()` 删除后，`owns_storage` 不再有保留价值，应一并移除。

### 7.8 ISR 边界

ISR 版本继续只保留“能做就做，不能做就返回”的非阻塞语义：

- `osal_queue_send_from_isr()`
- `osal_queue_recv_from_isr()`

它们不接受 `timeout_ms`，也不做循环等待。

## 8. `timer` 模块保留与文档补充

### 8.1 功能保持不变

`timer` 模块本次不改功能，保留现有能力：

1. `tick/uptime`
2. `delay_us()`
3. `delay_ms()`
4. 软件定时器 `create/start/stop/delete`
5. `SysTick` 自动初始化
6. `OSAL_PLATFORM_TICK_RATE_HZ` 可配置

### 8.2 文档新增内容

`timer` 文档需要补上系统 tick 防溢出机制的实现方法和原理。

重点要说明：

1. 为什么不能简单写 `now >= deadline`。
2. 为什么应使用差值比较，例如：

```c
(int32_t)(now - deadline) >= 0
```

3. 这种写法如何在 32 位 tick 回绕后，仍在有限时间窗口内保持正确判断。
4. 当前 OSAL 的哪些地方使用了这种比较方式。

## 9. `irq` / `platform` / DWT profiling

### 9.1 重构目标

把 DWT 的“平台原始能力”从 `osal_irq` 收口到 `osal_platform`，让 `irq` 只保留 IRQ 抽象和 profiling 的对外封装。

### 9.2 `irq` 保留职责

`osal_irq` 保留这些职责：

- `osal_irq_disable()`
- `osal_irq_enable()`
- `osal_irq_restore()`
- `osal_irq_is_in_isr()`
- profiling 对外查询接口

### 9.3 `platform` 新职责

`osal_platform` 承接 DWT 原始后端相关内容：

1. DWT 寄存器映射
2. 是否支持 `DWT CYCCNT` 的平台配置
3. DWT backend 初始化
4. 原始 cycle 读取
5. 当前 CPU 频率相关的时间换算基础

### 9.4 profiling 开关

profiling 改成独立可选功能，由：

- `OSAL_CFG_ENABLE_IRQ_PROFILE`

单独控制。

它与 `OSAL_CFG_ENABLE_DEBUG` 不再绑定。

### 9.5 平台支持边界

文档中明确写出：

1. `Cortex-M0/M0+` 不支持 `DWT CYCCNT`
2. `Cortex-M3/M4/M7` 可支持
3. 当前 `STM32F407` 作为 `Cortex-M4`，默认支持

## 10. 文档改写范围

### 10.1 需要重写或重命名的文档

本次至少要重写这些文档内容：

- `01-总览与移植步骤.md`
- `02-task-协作式任务调度.md`
- `03-timer-系统时基-软件定时器与延时.md`
- `04-queue-环形消息队列.md`
- `05-mem-静态堆与内存池.md`
- `06-irq-中断控制抽象.md`
- `07-cortexm-内核外设配置.md`
- `08-components-外围组件与板级示例.md`
- `README.md`

其中推荐做以下标题级调整：

1. `04-queue-环形消息队列.md`
   - 改成与新语义一致的同步超时队列文档标题
2. `06-irq-中断控制抽象.md` 与 `07-cortexm-内核外设配置.md`
   - 分别描述 `irq` 和 `cortexm`，不再把多个模块混在一篇里

### 10.2 文档内容要求

文档必须以“修改后的系统”为准，明确删除旧语义，不再保留历史说明。

每个模块下都要写清楚：

1. 模块职责
2. 对外公开函数
3. 关键内部函数与职责
4. 数据结构
5. 状态与状态切换
6. 使用边界
7. 非目标与限制

对 `task` 模块尤其要写清楚：

1. 任务结构体结构
2. 任务状态切换
3. 调度链表操作
4. `osal_run_priority_list()`
5. `osal_run_internal()`
6. `create/delete/start/stop/yield/run`

## 11. 示例与集成代码同步

本次除内核源码外，还需要同步修改：

1. `main.c` 示例
2. 任何依赖旧 `event/mutex/sleep/sleep_until/send_timeout/recv_timeout/create_static` 的示例代码
3. README 和模块说明文档里的示例调用

目标是让示例代码只展示修改后的真实模型：

- `task` 是纯协作式调度
- `yield` 是同步嵌套调度
- `queue(timeout_ms)` 是同步忙等超时重试

## 12. 验证要求

### 12.1 编译验证

至少要完成一次完整 Keil rebuild，确认：

- `0 Error`
- `0 Warning`

### 12.2 `task` 验证

至少验证：

1. `create/start/stop/delete` 正常工作
2. `yield()` 后当前任务能继续执行 `yield` 之后的语句
3. 删除 `sleep/sleep_until` 后无残留调用点

### 12.3 `queue` 验证

至少验证：

1. `timeout_ms = 0` 时的纯非阻塞发送/接收
2. `timeout_ms = N` 时成功路径
3. `timeout_ms = N` 时超时路径
4. ISR 版本接口仍正常工作
5. 删除 `create_static/send_timeout/recv_timeout` 后无残留引用

### 12.4 文档验证

至少检查：

1. 文档中不再出现“BLOCKED / wait list / event wake / mutex wait / deleted wake”这类旧语义描述
2. `task` 文档与源码结构一致
3. `queue` 文档与新接口签名一致
4. `timer` 文档已经补上防溢出原理

## 13. 风险与边界

### 13.1 最大行为变化

本次最大的行为变化有两类：

1. 旧的伪阻塞任务模型被明确移除。
2. `queue(timeout_ms)` 不再伪装成“任务挂起等待”，而是明确成为同步忙等超时重试。

### 13.2 使用者需要接受的边界

改造后 OSAL 仍然有这些明确边界：

1. 协作式，不是抢占式。
2. 没有任务上下文保存。
3. `queue(timeout_ms)` 不是 CPU 空闲等待。
4. 需要跨轮推进的复杂业务逻辑，仍应由调用方自己写状态机。

## 14. 结论

这次改造的核心不是“加更多功能”，而是把当前 OSAL 明确收口成一套边界真实、概念不打架的裸机协作式内核：

1. `task` 只做协作式调度，不再伪装阻塞恢复。
2. `queue` 只做固定项大小队列，并把 `timeout_ms` 写成真实的同步忙等语义。
3. `timer` 保持原样，补全文档解释。
4. `irq/platform` 的 DWT profiling 边界重新理顺。
5. 文档与源码以修改后的系统为准，不再同时维护两套互相冲突的语义。

如果按这份设计实现，当前 OSAL 会从“部分接口名字看起来像 RTOS，实际行为却不是”收口到“接口本身就准确表达真实模型”。
