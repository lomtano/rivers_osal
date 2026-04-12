# OSAL Boundary Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 收紧 OSAL 的等待状态码、对象删除恢复语义、system/platform 边界和工程 warning，让接口本身就能表达真实状态。

**Architecture:** 保留现有 `queue / event / mutex` 接口形状，只调整返回值语义和内部恢复路径。`task` 继续作为统一阻塞/恢复状态中心，`queue/event/mutex` 只负责各自等待链表和资源变化唤醒。`system/platform` 的收口重点是去掉 system 层不必要的弱符号残留，同时保留当前 SysTick/IRQ 总体方案不变。

**Tech Stack:** C, Keil MDK ARMCC5, Cortex-M SysTick, 当前 OSAL system/platform/components 代码结构

---

### Task 1: 收口公共状态码与公开接口契约

**Files:**
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Inc\osal.h`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Inc\osal_queue.h`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Inc\osal_event.h`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Inc\osal_mutex.h`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Inc\osal_task.h`

- [ ] **Step 1: 先把状态码契约改成单一语义**

将 `osal_status_t` 从当前定义：

```c
typedef enum {
    OSAL_OK = 0,
    OSAL_ERROR = 1,
    OSAL_ERR_TIMEOUT = 2,
    OSAL_ERR_RESOURCE = 3,
    OSAL_ERR_PARAM = 4,
    OSAL_ERR_NOMEM = 5,
    OSAL_ERR_ISR = 6,
    OSAL_RESERVED = 0x7FFFFFFF
} osal_status_t;
```

改成：

```c
typedef enum {
    OSAL_OK = 0,
    OSAL_ERROR = 1,
    OSAL_ERR_TIMEOUT = 2,
    OSAL_ERR_RESOURCE = 3,
    OSAL_ERR_BLOCKED = 4,
    OSAL_ERR_DELETED = 5,
    OSAL_ERR_PARAM = 6,
    OSAL_ERR_NOMEM = 7,
    OSAL_ERR_ISR = 8,
    OSAL_RESERVED = 0x7FFFFFFF
} osal_status_t;
```

- [ ] **Step 2: 更新总说明，明确 5 种等待结果**

把 `osal.h` 里的等待契约和资源契约收成下面这类描述：

```c
/*
 * 等待接口统一返回语义：
 * 1. OSAL_OK：本轮已经真正拿到资源。
 * 2. OSAL_ERR_RESOURCE：资源当前不可用，且本轮没有进入等待。
 * 3. OSAL_ERR_BLOCKED：当前任务已经被挂起，等待后续唤醒。
 * 4. OSAL_ERR_TIMEOUT：等待超时。
 * 5. OSAL_ERR_DELETED：等待期间，对象被删除。
 */
```

- [ ] **Step 3: 更新 queue 头文件，把 `OSAL_ERR_RESOURCE` 和 `OSAL_ERR_BLOCKED` 区分开**

把 `osal_queue.h` 里 `send_timeout()` / `recv_timeout()` 注释里的这类描述：

```c
 * - OSAL_ERR_RESOURCE：当前轮还未满足资源，任务已经被挂起或本次不等待失败。
```

改成：

```c
 * - OSAL_ERR_RESOURCE：资源当前不可用，且 timeout_ms=0U，本轮不等待。
 * - OSAL_ERR_BLOCKED：当前任务已进入 BLOCKED，并挂入等待链表。
 * - OSAL_ERR_DELETED：等待期间队列对象被删除。
```

- [ ] **Step 4: 更新 event 头文件，把 wait 的 0U / N / 永久等待写成新语义**

把 `osal_event_wait()` 的注释整理为：

```c
 * @note 0U 表示只检查一次当前事件状态，不等待。
 * @return
 * - OSAL_OK：当前轮已成功消费事件。
 * - OSAL_ERR_RESOURCE：事件未触发，且 timeout_ms=0U。
 * - OSAL_ERR_BLOCKED：当前任务已进入 BLOCKED，等待后续 set。
 * - OSAL_ERR_TIMEOUT：等待超时。
 * - OSAL_ERR_DELETED：等待期间事件对象被删除。
```

- [ ] **Step 5: 更新 mutex 头文件，把 lock 的返回值语义也改成同一套**

把 `osal_mutex_lock()` 的注释整理为：

```c
 * @return
 * - OSAL_OK：当前轮已拿到锁。
 * - OSAL_ERR_RESOURCE：锁当前不可用，且 timeout_ms=0U。
 * - OSAL_ERR_BLOCKED：当前任务已进入 BLOCKED，等待后续 unlock。
 * - OSAL_ERR_TIMEOUT：等待超时。
 * - OSAL_ERR_DELETED：等待期间互斥量对象被删除。
```

- [ ] **Step 6: 更新 task 头文件，补一段“恢复结果来自等待对象”的说明**

在 `osal_task.h` 里补这类说明：

```c
/*
 * task 本身不直接暴露等待对象接口。
 * queue/event/mutex 的等待结果会通过 task 内部恢复状态传播出来：
 * - BLOCKED
 * - TIMEOUT
 * - DELETED
 */
```

- [ ] **Step 7: 暂不编译，先做一遍头文件签名自检**

Run:

```powershell
rg -n "OSAL_ERR_BLOCKED|OSAL_ERR_DELETED|OSAL_ERR_RESOURCE" "A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Inc"
```

Expected:
- 头文件里能看到新状态码
- `queue/event/mutex` 注释不再把 `OSAL_ERR_RESOURCE` 解释成“已挂起”

- [ ] **Step 8: Commit**

```bash
git add A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal/system/Inc/osal.h A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal/system/Inc/osal_queue.h A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal/system/Inc/osal_event.h A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal/system/Inc/osal_mutex.h A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal/system/Inc/osal_task.h
git commit -m "refactor: clarify osal wait status contracts"
```

### Task 2: 收口 task 的阻塞/恢复核心语义

**Files:**
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Src\osal_task.c`

- [ ] **Step 1: 确认 `make_ready()` 只保留异常恢复结果，正常唤醒仍走 `OSAL_OK`**

保留这种核心结构：

```c
if (resume_status != OSAL_OK) {
    task->resume_reason = previous_reason;
    task->resume_object = previous_object;
    task->resume_status = resume_status;
    task->resume_valid = true;
}
```

但在注释里把 `resume_status` 的新合法集合写清楚：

```c
/* resume_status 现在只允许：
 * - OSAL_OK
 * - OSAL_ERR_TIMEOUT
 * - OSAL_ERR_DELETED
 */
```

- [ ] **Step 2: 保留 `osal_task_block_current_internal()` 的返回值为 `OSAL_OK`**

这里不要把内部阻塞核心改成直接返回 `OSAL_ERR_BLOCKED`。保持：

```c
task->state = OSAL_TASK_BLOCKED;
return OSAL_OK;
```

因为“是否向外返回 BLOCKED”应该由 `queue/event/mutex` 这些上层对象接口决定，不要让 task 内部状态机和公开 API 语义耦死。

- [ ] **Step 3: 把 timeout 恢复路径统一保留为 `OSAL_ERR_TIMEOUT`**

检查 `osal_task_check_wait_timeout()` 四类分支，保持：

```c
osal_task_make_ready(task, OSAL_ERR_TIMEOUT);
```

确认 `SLEEP` 分支仍然是：

```c
osal_task_make_ready(task, OSAL_OK);
```

这样 sleep/sleep_until 不被新的等待对象语义污染。

- [ ] **Step 4: 检查 `osal_task_consume_wait_result_internal()` 是否已经支持新状态码透传**

这里应保持简单透传：

```c
osal_status_t status = task->resume_status;
osal_task_clear_resume_state(task);
return status;
```

不在这里写对象类型分支，不让 task 核心知道 queue/event/mutex 的业务语义。

- [ ] **Step 5: 编译期自检 task 相关等待原因没有新增不一致**

Run:

```powershell
rg -n "OSAL_TASK_WAIT_|resume_status|OSAL_ERR_TIMEOUT|OSAL_ERR_DELETED" "A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Src\osal_task.c"
```

Expected:
- `task.c` 只处理“阻塞/恢复状态机”
- 不直接硬编码 `queue/event/mutex` 的外部返回值解释

- [ ] **Step 6: Commit**

```bash
git add A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal/system/Src/osal_task.c
git commit -m "refactor: keep task wait core object-agnostic"
```

### Task 3: 收口 queue 的返回值和删除恢复路径

**Files:**
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Src\osal_queue.c`

- [ ] **Step 1: 把“准备进入等待”从 `OSAL_ERR_RESOURCE` 改成 `OSAL_ERR_BLOCKED`**

把：

```c
return OSAL_ERR_RESOURCE;
```

改成：

```c
return OSAL_ERR_BLOCKED;
```

位置：
- `osal_queue_prepare_wait_locked()`

- [ ] **Step 2: 保留非阻塞 send/recv 的资源错误仍为 `OSAL_ERR_RESOURCE`**

这两处不要改：

```c
if (q->count >= q->length) {
    return OSAL_ERR_RESOURCE;
}
```

```c
if (q->count == 0U) {
    return OSAL_ERR_RESOURCE;
}
```

因为这是“资源当前不可用且本轮不等待”的正确语义。

- [ ] **Step 3: 删除队列时，把等待恢复状态从 `OSAL_ERR_RESOURCE` 改成 `OSAL_ERR_DELETED`**

把：

```c
osal_queue_wake_all_waiters_locked(current, OSAL_TASK_WAIT_QUEUE_SEND, OSAL_ERR_RESOURCE);
osal_queue_wake_all_waiters_locked(current, OSAL_TASK_WAIT_QUEUE_RECV, OSAL_ERR_RESOURCE);
```

改成：

```c
osal_queue_wake_all_waiters_locked(current, OSAL_TASK_WAIT_QUEUE_SEND, OSAL_ERR_DELETED);
osal_queue_wake_all_waiters_locked(current, OSAL_TASK_WAIT_QUEUE_RECV, OSAL_ERR_DELETED);
```

- [ ] **Step 4: 调整 `send_timeout()` 的等待返回判断**

把这一段的注释和行为改成：

```c
if ((status != OSAL_ERR_RESOURCE) || (timeout_ms == 0U)) {
    osal_irq_restore(irq_state);
    return status;
}

status = osal_queue_prepare_wait_locked(q, OSAL_TASK_WAIT_QUEUE_SEND, timeout_ms);
osal_irq_restore(irq_state);
return status;
```

并确保最终这里返回的是：
- `OSAL_ERR_BLOCKED`

不是旧的 `OSAL_ERR_RESOURCE`

- [ ] **Step 5: 调整 `recv_timeout()` 的等待返回判断**

做与 `send_timeout()` 对称的修改。

- [ ] **Step 6: 清掉 queue 当前的 `contains()` warning**

当前 warning 来自：

```c
static bool osal_queue_contains(osal_queue_t *q)
```

处理方式：
- 保留 `#if OSAL_CFG_ENABLE_DEBUG` 下的使用
- 把整个函数也包进 `#if OSAL_CFG_ENABLE_DEBUG`，或在 release 下根本不生成它

示例：

```c
#if OSAL_CFG_ENABLE_DEBUG
static bool osal_queue_contains(osal_queue_t *q) {
    ...
}
#endif
```

- [ ] **Step 7: 运行编译，确认 queue 相关语义和 warning 都收口**

Run:

```powershell
& 'D:\APP\Keil5\UV4\UV4.exe' -b 'A:\Embedded_system\cubemx_project\rivers_osal\MDK-ARM\rivers_osal.uvprojx' -o 'A:\Embedded_system\cubemx_project\rivers_osal\MDK-ARM\build_boundary_queue.log'
```

Expected:
- `osal_queue.c` 无 warning
- 工程仍能过编译

- [ ] **Step 8: Commit**

```bash
git add A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal/system/Src/osal_queue.c
git commit -m "refactor: split queue blocked and deleted statuses"
```

### Task 4: 收口 event 与 mutex 的等待返回值

**Files:**
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Src\osal_event.c`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Src\osal_mutex.c`

- [ ] **Step 1: event 进入等待时改返回 `OSAL_ERR_BLOCKED`**

把：

```c
return OSAL_ERR_RESOURCE;
```

改成：

```c
return OSAL_ERR_BLOCKED;
```

位置：
- `osal_event_prepare_wait_locked()`

- [ ] **Step 2: event 的 `timeout=0U` 语义改成真正的资源不可用**

把：

```c
return OSAL_ERR_TIMEOUT;
```

改成：

```c
return OSAL_ERR_RESOURCE;
```

位置：
- `osal_event_wait()` 中 `timeout_ms == 0U` 分支

因为“只检查一次当前状态，未触发”不是超时，而是本轮资源不满足。

- [ ] **Step 3: event 删除恢复改成 `OSAL_ERR_DELETED`**

把：

```c
osal_event_wake_all_waiters_locked(current, OSAL_ERR_RESOURCE);
```

改成：

```c
osal_event_wake_all_waiters_locked(current, OSAL_ERR_DELETED);
```

- [ ] **Step 4: mutex 进入等待时改返回 `OSAL_ERR_BLOCKED`**

把：

```c
return OSAL_ERR_RESOURCE;
```

改成：

```c
return OSAL_ERR_BLOCKED;
```

位置：
- `osal_mutex_prepare_wait_locked()`

- [ ] **Step 5: mutex 的 `timeout=0U` 分支改成 `OSAL_ERR_RESOURCE`**

把：

```c
return OSAL_ERR_TIMEOUT;
```

改成：

```c
return OSAL_ERR_RESOURCE;
```

位置：
- `osal_mutex_lock()` 中 `timeout_ms == 0U` 分支

因为这也是“本轮不等待，锁不可用”，不是“已经等过并超时”。

- [ ] **Step 6: mutex 删除恢复改成 `OSAL_ERR_DELETED`**

把：

```c
osal_mutex_wake_all_waiters_locked(current, OSAL_ERR_RESOURCE);
```

改成：

```c
osal_mutex_wake_all_waiters_locked(current, OSAL_ERR_DELETED);
```

- [ ] **Step 7: 清掉 event/mutex 当前 `contains()` warning**

像 queue 一样，把：

```c
static bool osal_event_contains(...)
static bool osal_mutex_contains(...)
```

包到：

```c
#if OSAL_CFG_ENABLE_DEBUG
...
#endif
```

- [ ] **Step 8: 编译验证 event/mutex 语义和 warning**

Run:

```powershell
& 'D:\APP\Keil5\UV4\UV4.exe' -b 'A:\Embedded_system\cubemx_project\rivers_osal\MDK-ARM\rivers_osal.uvprojx' -o 'A:\Embedded_system\cubemx_project\rivers_osal\MDK-ARM\build_boundary_event_mutex.log'
```

Expected:
- `event.c` / `mutex.c` 无 warning
- 工程仍然 0 error

- [ ] **Step 9: Commit**

```bash
git add A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal/system/Src/osal_event.c A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal/system/Src/osal_mutex.c
git commit -m "refactor: split event and mutex blocked/deleted statuses"
```

### Task 5: 收口 platform 弱符号残留、示例和文档

**Files:**
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Src\osal_platform.c`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Core\Src\main.c`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\platform\example\stm32f4\osal_integration_stm32f4.c`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\README.md`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\CHANGELOG.md`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\docs\*.md`（仅相关章节）
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\components\periph\flash\Src\periph_flash.c`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\components\periph\usart\Src\periph_uart.c`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Src\osal_mem.c`

- [ ] **Step 1: 去掉 system 层 `osal_platform_init()` 的弱符号壳**

当前代码：

```c
__weak void osal_platform_init(void) {
}
```

处理目标：
- 若该函数只是空壳，则改成普通函数声明 + 普通实现
- 或改成明确的 system 内部入口，不再通过弱符号让 board example 覆盖

优先方案：

```c
void osal_platform_init(void) {
    /* 默认无需额外板级初始化。 */
}
```

前提是确认 board example 当前没有依赖强定义覆盖它。

- [ ] **Step 2: 调整 `main.c` 示例里的等待判断**

把原来这类判断：

```c
if (status == OSAL_OK) {
    ...
} else if (status == OSAL_ERR_RESOURCE) {
    return;
}
```

改成：

```c
if (status == OSAL_OK) {
    ...
} else if (status == OSAL_ERR_BLOCKED) {
    return;
} else if (status == OSAL_ERR_DELETED) {
    return;
}
```

至少覆盖：
- queue 发送任务
- queue 接收任务
- event 示例
- mutex 示例

- [ ] **Step 3: 调整 integration 示例，统一使用新状态码**

把 `osal_integration_stm32f4.c` 里所有等待接口的说明和分支同步改成：

```c
if (status == OSAL_OK) {
    ...
} else if (status == OSAL_ERR_BLOCKED) {
    return;
} else if (status == OSAL_ERR_TIMEOUT) {
    ...
} else if (status == OSAL_ERR_DELETED) {
    ...
}
```

- [ ] **Step 4: 清掉剩余 `contains()` warning**

处理：
- `osal_mem.c`
- `periph_flash.c`
- `periph_uart.c`

做法与前面一致：只在 debug 打开时生成相关辅助函数。

- [ ] **Step 5: 更新 README / CHANGELOG / docs 中的旧语义**

把所有这类描述：

```md
OSAL_ERR_RESOURCE 表示当前任务已经挂起等待
```

改成：

```md
OSAL_ERR_BLOCKED 表示当前任务已经进入 BLOCKED
OSAL_ERR_RESOURCE 只表示当前资源不可用且本轮不等待
OSAL_ERR_DELETED 表示等待期间对象被删除
```

- [ ] **Step 6: 最终编译，目标是 0 error / 0 warning**

Run:

```powershell
& 'D:\APP\Keil5\UV4\UV4.exe' -b 'A:\Embedded_system\cubemx_project\rivers_osal\MDK-ARM\rivers_osal.uvprojx' -o 'A:\Embedded_system\cubemx_project\rivers_osal\MDK-ARM\build_boundary_final.log'
```

Expected:
- `0 Error(s), 0 Warning(s)`

- [ ] **Step 7: 最终提交**

```bash
git add A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal A:/Embedded_system/cubemx_project/rivers_osal/Core/Src/main.c
git commit -m "refactor: tighten osal wait semantics and platform boundary"
```

