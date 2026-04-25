# Rivers OSAL

`Rivers OSAL` 是一套面向 Cortex-M 裸机产品的轻量级 OSAL。

它不是 RTOS 子集，也不以模拟 RTOS API 为目标；它做的是把裸机工程中常见的任务轮询、系统时基、消息缓冲、静态内存管理、中断封装和 Cortex-M 内核外设配置统一管理起来，让产品代码比传统 `while(1) + flag + counter` 更清晰、更可维护。

## 当前定位

- 面向 Cortex-M 裸机产品
- 保留裸机系统的确定性和可控性
- 提供协作式任务调度，不提供抢占式上下文切换
- 不提供假的阻塞、等待链表、任务唤醒或上下文恢复语义
- 适合状态机式业务代码、外设轮询、ISR/DMA 与任务侧的数据传递

## 核心特性

- `task`
  基于 `HIGH / MEDIUM / LOW` 优先级的协作式任务调度。
  应用完成初始化后调用 `osal_start_system()`，OSAL 接管顶层循环，正常情况下不再返回 `main()`。

- `timer`
  提供统一系统时基、`us/ms` 级忙等延时、运行时间获取和软件定时器。
  软件定时器在任务态轮询执行，避免在中断里运行复杂回调。

- `queue`
  固定单元大小的环形消息队列。
  支持自定义元素类型、元素大小和队列长度，适合任务、ISR、DMA 等路径之间传递固定格式消息。

- `mem`
  不依赖 C 标准库 `malloc/free`。
  提供统一静态堆和固定块内存池：统一堆负责变长低频对象分配，固定块池负责固定大小、高频、ISR 可参与的对象申请与归还。

- `irq`
  对 CMSIS 中断开关、状态恢复和 ISR 上下文判断做轻量封装，避免应用层散落底层中断控制调用。

- `cortexm`
  统一配置 OSAL 依赖的 Cortex-M 内核外设，包括 `SysTick`、`NVIC Group 4`、`SysTick` 优先级和 `DWT CYCCNT`。
  当前预留 `MPU` 扩展位置。

## 产品化优点

- 模块边界清晰：`system` 负责 OSAL 内核，`cortexm` 负责内核外设，`platform/example` 负责板级桥接，`components` 负责可选外围组件。
- 运行模型真实：不隐藏协作式调度的本质，不把 queue 包装成假的阻塞唤醒模型。
- 内存来源可控：OSAL 内核对象默认来自 `osal_mem` 管理的静态内存，不依赖堆库实现。
- ISR 边界明确：任务态接口和 ISR 接口分开，统一堆禁止 ISR 使用，固定块池支持 ISR 快速申请/归还。
- 调试能力完整：`OSAL_DEBUG_HOOK` 可报告非法句柄、错误上下文、二次释放、错误归还等问题。
- 临界区可观测：DWT profiling 可统计 OSAL system 层内部临界区耗时，便于评估实时性影响。
- 代码可裁剪：通过 `osal_config.h` 控制 queue、软件定时器、USART、Flash、debug、DWT profiling 等功能开关。

## 当前使用边界

- 任务函数必须短小，做完一小段工作就返回。
- 跨多轮业务应写成状态机。
- `queue(timeout_ms)` 是基于 tick 的同步忙等重试，不是任务挂起。
- `osal_timer_delay_us()` / `osal_timer_delay_ms()` 是忙等延时，会占用 CPU。
- 软件定时器回调不应执行重任务。
- 运行期高频固定大小对象应优先使用 `osal_mempool`，不要反复走统一堆。

## 仓库结构

```text
rivers_osal/
|-- osal/                         独立 OSAL 源码与文档
|-- STM32F407ZGT6移植工程/        当前 STM32F407 示例工程
|-- LICENSE
`-- README.md
```

当前工作工程中的 OSAL 位于：

```text
Middleware/osal/
```

GitHub 仓库根目录也保留一份独立 `osal/`，方便单独查看和复用 OSAL。

## 最小接入流程

1. 把 `osal/system/Inc` 加入头文件路径。
2. 把 `osal/system/Src` 中需要的源文件加入工程。
3. 根据目标板修改 `osal_config.h` 和 `osal_cortexm.h`。
4. 在应用层包含 `osal.h`。
5. 完成硬件初始化后调用 `osal_init()`。
6. 创建并启动任务、队列、软件定时器或组件。
7. 调用 `osal_start_system()`，由 OSAL 接管顶层调度循环。
8. 在 `SysTick_Handler()` 中调用 `osal_tick_handler()`。

## 推荐阅读

- `osal/README.md`
- `osal/docs/01-总览与移植步骤.md`
- `osal/docs/02-task-协作式任务调度.md`
- `osal/docs/03-timer-系统时基-软件定时器与延时.md`
- `osal/docs/04-queue-环形消息队列.md`
- `osal/docs/05-mem-静态堆与内存池.md`
- `osal/docs/06-irq-中断控制抽象.md`
- `osal/docs/07-cortexm-内核外设配置.md`
- `osal/docs/08-components-外围组件与板级示例.md`
