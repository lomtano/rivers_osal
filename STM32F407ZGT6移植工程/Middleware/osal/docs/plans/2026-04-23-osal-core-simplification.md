# OSAL Core Simplification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把当前 OSAL 收口成纯协作式裸机内核，删除 `event/mutex` 和伪阻塞任务语义，引入统一配置头，并把 `queue` 改成同步 `timeout_ms` 忙等模型。

**Architecture:** `task` 只保留协作式调度与 `yield`；`queue` 继续使用环形缓冲区，但删掉等待链表和 `create_static/send_timeout/recv_timeout`，统一成同步 `send/recv(timeout_ms)`；`irq/platform` 重新划分 DWT profiling 的原始能力边界，`timer` 保持功能不变，文档整体按修改后的系统重写。

**Tech Stack:** C, Keil MDK ARMCC5, Cortex-M SysTick, 当前 OSAL system/platform/components 结构

---

### Task 1: 建立统一配置头并收缩公共入口

**Files:**
- Create: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Inc\osal_config.h`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Inc\osal.h`

- [x] 新建 `osal_config.h`，承载 `OSAL_CFG_ENABLE_DEBUG / OSAL_CFG_ENABLE_QUEUE / OSAL_CFG_ENABLE_IRQ_PROFILE / OSAL_CFG_ENABLE_SW_TIMER / OSAL_CFG_ENABLE_USART / OSAL_CFG_ENABLE_FLASH / OSAL_CFG_INCLUDE_PLATFORM_HEADER / OSAL_PLATFORM_HEADER_FILE`
- [x] 从 `osal.h` 中移除功能开关定义，只保留状态码、调试宏、聚合头和基础契约
- [x] 删除 `OSAL_ERR_BLOCKED / OSAL_ERR_DELETED / OSAL_WAIT_FOREVER`
- [x] 从 `osal.h` 中移除 `osal_event.h / osal_mutex.h` 聚合

### Task 2: 精简 task 模块为纯协作式调度

**Files:**
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Inc\osal_task.h`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Src\osal_task.c`

- [x] 删除 `OSAL_TASK_BLOCKED` 和 `osal_task_wait_reason_t`
- [x] 删除 `osal_task_sleep()` / `osal_task_sleep_until()` 的声明与实现
- [x] 删除 `wait_* / resume_* / periodic_* / dispatch_tick_ms / wait_next` 相关字段和内部函数
- [x] 保留并整理 `create / delete / start / stop / yield / run`
- [x] 保留优先级链表、低优先级补偿扫描和嵌套调度深度控制
- [x] 保证 `yield()` 仍然是“同步嵌套调度后继续当前执行流”

### Task 3: 把 queue 收口成同步 timeout_ms API

**Files:**
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Inc\osal_queue.h`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Src\osal_queue.c`

- [x] 删除 `osal_queue_create_static()`、`osal_queue_send_timeout()`、`osal_queue_recv_timeout()`
- [x] 把 `osal_queue_send()` / `osal_queue_recv()` 改成三参版本，第三个参数为 `timeout_ms`
- [x] 明确 `timeout_ms=0` 为立即尝试，资源不足返回 `OSAL_ERR_RESOURCE`
- [x] 明确 `timeout_ms=N` 为基于系统 tick 的同步忙等重试，成功返回 `OSAL_OK`，超时返回 `OSAL_ERR_TIMEOUT`
- [x] 删除 `OSAL_WAIT_FOREVER` 支持
- [x] 删除 `wait_send_list / wait_recv_list / owns_storage` 及所有等待链表逻辑
- [x] 队列数据区统一走 `osal_mem_alloc()`，删除用户静态缓冲区入口
- [x] 保留 ISR 版本的立即尝试接口

### Task 4: 删除 event/mutex 模块并清理引用

**Files:**
- Delete: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Inc\osal_event.h`
- Delete: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Inc\osal_mutex.h`
- Delete: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Src\osal_event.c`
- Delete: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Src\osal_mutex.c`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\MDK-ARM\rivers_osal.uvprojx`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Core\Src\main.c`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\platform\example\stm32f4\osal_integration_stm32f4.c`

- [x] 删除源码和头文件
- [x] 从工程文件中移除 `osal_event.c / osal_mutex.c`
- [x] 删除示例中的 event/mutex 演示和相关说明

### Task 5: 调整 irq/platform 与 DWT profiling

**Files:**
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Inc\osal_platform.h`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Src\osal_platform.c`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Inc\osal_irq.h`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\system\Src\osal_irq.c`

- [x] 让 profiling 是否启用只受 `OSAL_CFG_ENABLE_IRQ_PROFILE` 控制
- [x] 把 DWT 原始寄存器、backend init、原始 cycle 读取下沉到 `platform`
- [x] 让 `irq` 只保留 IRQ 抽象与 profiling 统计接口
- [x] 保持 `OSAL_PLATFORM_CPU_CLOCK_HZ` 时间换算不变

### Task 6: 更新 docs 与示例说明

**Files:**
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\docs\01-总览与移植步骤.md`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\docs\02-task-协作式任务调度.md`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\docs\03-timer-系统时基-软件定时器与延时.md`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\docs\04-queue-环形消息队列.md`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\docs\05-mem-静态堆与内存池.md`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\docs\06-irq-中断控制抽象.md`
- Add: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\docs\07-cortexm-内核外设配置.md`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\docs\08-components-外围组件与板级示例.md`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\docs\README.md`
- Modify: `A:\Embedded_system\cubemx_project\rivers_osal\Middleware\osal\CHANGELOG.md`

- [x] 文档全部以修改后的 OSAL 为准，不保留旧语义说明
- [x] `task` 文档写清结构体、状态切换、链表操作、调度入口、`yield/start_system`
- [x] `queue` 文档改成同步超时重试模型，写清忙等边界
- [x] `timer` 文档补充 tick 防溢出机制实现方法与原理
- [x] `06` 文档改成仅覆盖 `irq/platform`

### Task 7: 完整 rebuild 验证

**Files:**
- Verify: `A:\Embedded_system\cubemx_project\rivers_osal\MDK-ARM\build_boundary_rebuild.log`

- [x] 执行一次 Keil `rebuild`
- [x] 修到 `0 Error(s), 0 Warning(s)`
- [x] 检查示例代码与头文件无旧接口残留
