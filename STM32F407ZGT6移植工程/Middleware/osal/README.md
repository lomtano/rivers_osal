# OSAL 说明

`OSAL` 是一套面向 Cortex-M 裸机工程的纯协作式内核。
当前版本不再尝试模拟一个精简 RTOS，而是把真实能力收口成一组边界清晰、实现和文档一致的基础模块：

- `task`：协作式任务调度，按优先级决定调度顺序
- `timer`：统一时基、`us/ms` 忙等延时、软件定时器
- `queue`：固定单元大小的环形消息队列，任务态提供同步 `timeout_ms` 重试接口
- `mem`：基于静态内存池的动态分配算法，主要服务于内核对象
- `irq`：对 CMSIS 中断开关接口的轻量封装
- `cortexm`：SysTick、NVIC Group (4)、DWT 等 Cortex-M 内核外设配置

`event` 和 `mutex` 已从当前公开接口与工程编译单元中移除。

## 特性与优点

- 面向 Cortex-M 裸机产品，不以模拟 RTOS API 为目标，保留裸机系统的确定性和可控性。
- 支持 `SysTick`、`NVIC Group 4`、`SysTick` 优先级和 `DWT CYCCNT` 这类 Cortex-M 内核外设配置。
- 提供基于优先级的协作式任务调度，`osal_start_system()` 启动后接管顶层循环，正常情况下不返回 `main()`。
- 提供统一系统时基、`us/ms` 忙等延时、运行时间获取和软件定时器，并支持运行中动态调整软件定时器周期和剩余计数。
- 提供固定单元大小的环形消息队列，支持自定义元素类型、元素大小和队列长度。
- 提供任务态 queue 接口和 ISR queue 接口；ISR 接口始终是立即尝试，不做等待。
- 提供统一静态堆和固定块内存池，不依赖 C 标准库 `malloc/free`。
- 统一堆支持默认内部静态堆，也支持用户传入外部 heap buffer，方便放到指定 RAM 区域。
- 固定块内存池适合固定大小、高频、ISR 可参与的对象申请与归还。
- 提供轻量 `irq` 封装，统一关中断、恢复中断状态和 ISR 上下文判断。
- 提供 DWT 临界区 profiling，可观察 OSAL system 层内部临界区的最大、最小、平均耗时。
- 提供统一 debug hook，可报告非法句柄、错误上下文调用、二次释放、错误归还等问题。
- 通过 `osal_config.h` 集中裁剪 queue、软件定时器、USART、Flash、debug、DWT profiling 等功能。

## 目录结构

```text
osal/
|-- system/
|   |-- Inc/
|   `-- Src/
|-- platform/
|   `-- example/
|       `-- stm32f4/
|-- components/
|-- docs/
`-- CHANGELOG.md
```

## 当前核心模型

- `task` 没有独立任务栈，也不保存/恢复 CPU 上下文。
- 任务函数本质上是“被调度器重复调用的普通 C 函数”。
- `osal_task_yield()` 的语义是在当前调用栈里同步触发一次嵌套调度，返回后继续当前执行流。
- `queue` 本质上是一个使用 `osal_mem` 动态分配底层存储的固定单元环形缓冲区。
- 当前 `queue` 没有等待链表，也不会在 `send/recv` 后自动把等待任务切回 `READY`。
- `queue(timeout_ms)` 不是任务挂起语义。
- `timeout_ms = 0U` 表示立即尝试一次，资源不足返回 `OSAL_ERR_RESOURCE`。
- `timeout_ms = N` 表示在 `N ms` 窗口内基于系统 tick 同步忙等重试，成功返回 `OSAL_OK`，到期返回 `OSAL_ERR_TIMEOUT`。
- `osal_timer_delay_us()` / `osal_timer_delay_ms()` 仍然是忙等延时，会占用 CPU。
- 软件定时器由 `osal_start_system()` 内部持续调用 `osal_timer_poll()` 驱动。
- 软件定时器可以通过 `osal_timer_set_period()` 修改周期，通过 `osal_timer_set_remaining()` 修改当前剩余计数。
- `osal_start_system()` 每轮调度后会调用 `OSAL_IDLE_HOOK()`，默认空操作，可用于接入低功耗 idle 处理。

## 模块边界

- `system` 负责 OSAL 内核本身，不依赖具体板级驱动逻辑。
- `system/cortexm` 负责 OSAL 内核真正依赖的 SysTick、NVIC、DWT、CMSIS 宏映射。
- `platform/example/stm32f4` 负责当前 STM32F407 示例工程的 LED、USART、Flash 等桥接。
- `components` 放在 OSAL 之上的外围组件，目前包含 `USART`、`Flash`、`RTT`、`KEY`。

## 统一配置入口

`system/Inc/osal_config.h` 是统一配置头，当前集中承载这些宏：

- `OSAL_CFG_ENABLE_DEBUG`
- `OSAL_CFG_ENABLE_QUEUE`
- `OSAL_CFG_ENABLE_IRQ_PROFILE`
- `OSAL_CFG_ENABLE_SW_TIMER`
- `OSAL_CFG_ENABLE_USART`
- `OSAL_CFG_ENABLE_FLASH`
- `OSAL_CFG_INCLUDE_PLATFORM_HEADER`
- `OSAL_PLATFORM_HEADER_FILE`

应用层通常只需要包含 `osal.h`，它会先包含 `osal_config.h`，再聚合各模块头文件。

## 最小接入步骤

1. 把 `osal/system/Inc`、`osal/system/Src` 和你需要的 `platform/components` 文件加入工程。
2. 根据目标板修改 `osal_config.h` 和 `osal_cortexm.h` 中的相关配置。
3. 应用层包含 `osal.h`。
4. 硬件初始化完成后调用 `osal_init()`。
5. 创建并启动任务、组件或软件定时器后调用 `osal_start_system()`，正常情况下不会再返回。
6. 在 `SysTick_Handler()` 中调用 `osal_tick_handler()`。

## 使用边界

- 这套调度器是协作式，不是抢占式。
- 任务函数应保持短小，做完一小段工作就返回。
- 如果业务需要“跨多轮逐步推进”，应在任务层自己维护状态机。
- `queue(timeout_ms > 0)` 只适合资源状态可能被 ISR、DMA 或其他异步硬件路径改变的场景。
- 如果资源状态只能靠别的协作任务推进，任务里应优先使用 `timeout_ms = 0U`，由应用自己决定何时重试。
- 如果你需要“发送后唤醒等待该队列的任务”，当前 queue 不提供这层等待/恢复语义。
- 统一堆 `osal_mem_alloc/free` 只允许任务态使用；ISR 中固定大小对象应使用 `osal_mempool_alloc/free` 或预分配缓冲区。

## 推荐阅读顺序

1. `docs/01-总览与移植步骤.md`
2. `docs/02-task-协作式任务调度.md`
3. `docs/03-timer-系统时基-软件定时器与延时.md`
4. `docs/04-queue-环形消息队列.md`
5. `docs/05-mem-静态堆与内存池.md`
6. `docs/06-irq-中断控制抽象.md`
7. `docs/07-cortexm-内核外设配置.md`
8. `docs/08-components-外围组件与板级示例.md`

## 示例入口

- 当前工程主示例：`Core/Src/main.c`
- 可复制示例集合：`platform/example/stm32f4/osal_integration_stm32f4.c`
