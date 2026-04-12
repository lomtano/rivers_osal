# OSAL 边界收口设计

日期：2026-04-12

## 1. 目标

这次收口的目标不是继续加功能，而是把当前 OSAL 里几个已经能跑、但边界仍然不够干净的地方收紧，避免后续继续靠注释解释语义而不是靠接口本身表达语义。

本次只处理以下问题：

1. `OSAL_ERR_RESOURCE` 被复用了多种含义，导致 `queue / event / mutex` 的等待接口不够直观。
2. `event / mutex / queue` 的等待路径虽然已经统一成事件驱动，但返回值语义还没有统一收口。
3. `system / platform` 边界里仍有少量弱符号残留，不符合当前“核心在 system，板级桥接在 platform/example”的方向。
4. 当前工程还有少量 warning 没清掉，说明实现边界还有收尾工作。

这次不处理以下问题：

1. 不把 `mutex` 扩展到 ISR。
2. 不扩展 `event` 的 ISR 等待接口。
3. 不改任务调度模型，不把协作式改成抢占式。
4. 不改 `PRIMASK / BASEPRI` 策略。

## 2. 设计原则

### 2.1 状态码一义化

每个状态码尽量只表达一种主要语义，避免“同一个返回值在不同路径里表示完全不同的事情”。

目标是：

- `OSAL_OK`：操作完成，资源已真正拿到或状态已真正变更。
- `OSAL_ERR_RESOURCE`：资源当前不可用，而且本次不会进入等待。
- `OSAL_ERR_BLOCKED`：当前任务已经被挂起，等待后续事件唤醒。
- `OSAL_ERR_TIMEOUT`：等待超时。
- `OSAL_ERR_DELETED`：等待期间对象被删除，等待被中断。
- `OSAL_ERR_ISR`：当前上下文不允许调用该接口。

这次新增：

- `OSAL_ERR_BLOCKED`
- `OSAL_ERR_DELETED`

### 2.2 保留接口形状，收口接口语义

本次不把接口拆成 `try_xxx()` / `wait_xxx()` 两套，而是在保留现有接口名的前提下，把返回值语义收干净。

原因：

1. 现有 `queue / event / mutex` 示例和调用点已经围绕当前接口组织。
2. 事件驱动等待的底层实现已经具备，不需要靠大规模 API 重命名才能收口。
3. 这次要解决的是“边界脏”，不是“推翻重做”。

### 2.3 ISR 能力矩阵保持现状，但写清楚

本次不为了接口对称性而继续扩展 ISR API。

最终能力边界保持为：

- `queue`
  - 任务态：`send / recv / send_timeout / recv_timeout`
  - ISR：`send_from_isr / recv_from_isr`
- `event`
  - 任务态：`create / delete / wait`
  - ISR：`set / clear`
- `mutex`
  - 仅任务态：`create / delete / lock / unlock`

### 2.4 system 不反向依赖板级实例

`system` 只依赖 OSAL 自己定义的抽象和配置，不依赖具体板级实例。

`platform/example/<board>` 只做板级桥接：

- UART
- Flash
- LED
- 其他和 MCU SDK 直接耦合的内容

`system` 里不再保留多余的板级弱符号入口。

## 3. 模块级设计

### 3.1 `osal.h`

#### 设计变更

1. 新增状态码：
   - `OSAL_ERR_BLOCKED`
   - `OSAL_ERR_DELETED`
2. 更新统一等待语义说明。
3. 更新资源契约，明确：
   - “进入阻塞等待”不再用 `OSAL_ERR_RESOURCE` 表达
   - “等待对象被删除”不再复用普通资源错误

#### 预期效果

调用者看 `osal_status_t` 时，就能直接猜出这次调用处于哪个阶段：

- 成功
- 不等失败
- 已挂起
- 超时
- 对象被删

### 3.2 `queue`

#### 当前问题

`send_timeout()` / `recv_timeout()` 在资源暂时不满足、但允许等待时，返回 `OSAL_ERR_RESOURCE`。

这个返回值实际可能表示两种完全不同的情况：

1. 本次不等待，所以失败。
2. 当前任务已经被挂起，等待下一次被唤醒。

#### 收口方案

- `osal_queue_send()`
  - 队列满：`OSAL_ERR_RESOURCE`
- `osal_queue_recv()`
  - 队列空：`OSAL_ERR_RESOURCE`
- `osal_queue_send_timeout()`
  - 立即成功：`OSAL_OK`
  - 不等待且队列满：`OSAL_ERR_RESOURCE`
  - 进入等待：`OSAL_ERR_BLOCKED`
  - 超时：`OSAL_ERR_TIMEOUT`
  - 等待期间队列对象被删：`OSAL_ERR_DELETED`
- `osal_queue_recv_timeout()`
  - 与上面对称

#### 示例层约定

任务函数里推荐写法改成：

1. `OSAL_OK`：继续处理拿到资源后的逻辑。
2. `OSAL_ERR_BLOCKED`：本轮直接 `return`，等调度器下次重入。
3. `OSAL_ERR_TIMEOUT / OSAL_ERR_DELETED / OSAL_ERR_RESOURCE`：按业务需要处理。

### 3.3 `event`

#### 当前问题

`wait()` 已经是事件驱动等待模型，但：

- 进入等待和普通资源不可用没有彻底拆开
- 对象删除恢复时也仍然复用资源错误

#### 收口方案

- `osal_event_wait()`
  - 事件当前已满足：`OSAL_OK`
  - `timeout=0` 且事件未满足：`OSAL_ERR_RESOURCE`
  - 进入等待：`OSAL_ERR_BLOCKED`
  - 超时：`OSAL_ERR_TIMEOUT`
  - 事件对象删除：`OSAL_ERR_DELETED`

#### ISR 边界

保持：

- `set / clear` 可在 ISR 中用
- `wait` 不允许在 ISR 中用，继续返回 `OSAL_ERR_ISR`

### 3.4 `mutex`

#### 当前问题

`lock()` 已经切换成等待链表 + 事件驱动唤醒，但返回值语义还没有像队列一样彻底收口。

#### 收口方案

- `osal_mutex_lock()`
  - 立即拿到锁：`OSAL_OK`
  - `timeout=0` 且锁不可用：`OSAL_ERR_RESOURCE`
  - 进入等待：`OSAL_ERR_BLOCKED`
  - 超时：`OSAL_ERR_TIMEOUT`
  - 互斥量对象被删：`OSAL_ERR_DELETED`
- `osal_mutex_unlock()`
  - 正常解锁：`OSAL_OK`
  - 非法句柄或错误上下文：保留现有错误语义

#### 非目标

本次不处理：

- owner 校验
- 递归锁
- 优先级继承

### 3.5 `task`

#### 当前问题

任务内部已经有等待原因和等待对象，但对外语义还没有通过返回值完全体现出来。

#### 收口方案

任务控制块继续维护：

- 等待原因
- 等待对象
- 等待起点
- 绝对截止时间
- 恢复状态

但恢复状态的值这次统一改成与新状态码一致：

- 正常唤醒并拿到资源：下一次重入时应表现为 `OSAL_OK`
- 被挂起成功：当前轮返回 `OSAL_ERR_BLOCKED`
- 超时恢复：`OSAL_ERR_TIMEOUT`
- 对象删除恢复：`OSAL_ERR_DELETED`

### 3.6 `platform`

#### 当前问题

`system` 层仍保留了弱符号 `osal_platform_init()`，这和当前想要的“核心在 system，实例在 platform/example”的边界不完全一致。

#### 收口方案

1. 检查 `osal_platform_init()` 是否只是默认空实现。
2. 若它仅作为兼容壳存在，则改成更明确的 system 内部实现或静态内部入口。
3. 不再让 `system` 对板级实例依赖弱符号覆写。

#### 说明

板级示例里的 LED 弱函数可以视作示例层桥接，不属于本次必须清理的核心边界问题；本次重点先清理 system 层残留。

## 4. 示例与文档同步策略

这次必须同步修改：

- `main.c`
- `osal_integration_stm32f4.c`
- `README.md`
- `CHANGELOG.md`
- 相关头文件注释

示例里所有等待接口都统一成以下判断模式：

1. `OSAL_OK`
   - 说明当前轮已经真正完成动作。
2. `OSAL_ERR_BLOCKED`
   - 说明任务已经挂起，本轮必须立刻 `return`。
3. `OSAL_ERR_TIMEOUT`
   - 说明等待超时。
4. `OSAL_ERR_DELETED`
   - 说明等待对象在等待期间被删。
5. `OSAL_ERR_RESOURCE`
   - 只表示“当前不等待且资源不可用”。

## 5. warning 清理策略

当前 6 个 warning 都是未使用的 `contains()` 辅助函数。

本次处理原则：

1. 如果辅助函数确实已无意义，直接删除。
2. 如果只在 debug 分支下使用，则保证：
   - debug 打开时确实引用到
   - debug 关闭时不产生未使用 warning

目标是把工程收回到：

- `0 Error`
- `0 Warning`

## 6. 测试与验证

本次验证至少覆盖：

### 6.1 编译验证

- Keil 工程完整编译
- 确认 warning 清零

### 6.2 队列验证

1. 非阻塞发送/接收返回值是否正确
2. 超时等待时是否先返回 `OSAL_ERR_BLOCKED`
3. 唤醒后下一轮是否走到成功路径
4. 删除对象时等待任务是否得到 `OSAL_ERR_DELETED`

### 6.3 事件验证

1. `timeout=0` 未触发是否返回 `OSAL_ERR_RESOURCE`
2. 允许等待时是否返回 `OSAL_ERR_BLOCKED`
3. `set()` 后是否唤醒
4. `delete()` 后是否给 `OSAL_ERR_DELETED`

### 6.4 互斥量验证

1. 立即拿锁
2. 不等待拿锁失败
3. 等待锁时进入 `BLOCKED`
4. `unlock()` 唤醒等待任务
5. 删除互斥量时等待任务恢复为 `OSAL_ERR_DELETED`

## 7. 风险与边界

### 7.1 兼容性风险

这次会改变等待接口的返回值语义，因此所有示例和已有调用点都要同步检查。

### 7.2 非目标风险

即使这次把边界收口了，也仍然保留以下系统级边界：

- 协作式调度，不是抢占式
- `mutex` 无优先级继承
- `event` 只支持 `set/clear` 的 ISR 路径
- `PRIMASK` 临界区策略保持不变

## 8. 结论

本次收口的核心不是“加更多功能”，而是把现有功能的接口边界变得可预期：

1. 状态码一义化
2. 等待语义在 `queue / event / mutex` 间统一
3. ISR 能力矩阵明确但不盲目扩展
4. system/platform 边界再收紧一层
5. warning 清零

如果这份设计按预期实现，当前 OSAL 会从“功能已基本齐全，但部分语义靠文档解释”收口到“接口本身就更能表达真实状态”。
