# irq / cortexm / platform-example

> 注：本文文件名沿用旧命名，当前内容已经不再解释 `event / mutex`，而是聚焦 `irq / cortexm / platform-example` 的现行边界。

## 1. 模块分工

当前 `irq`、`cortexm` 和 `platform/example` 的边界如下：

- `irq`
  只保留中断开关、状态恢复和 ISR 上下文判断这组最薄的公共接口。
- `cortexm`
  承担 SysTick、NVIC Group (4)、DWT profiling backend，以及 CMSIS 宏映射。
- `platform/example`
  承担 LED、USART、Flash 这类板级外设示例桥接。

换句话说：
- `irq` 负责“对外怎么用中断控制”。
- `cortexm` 负责“内核到底怎么配置和测量 Cortex-M 外设”。
- `platform/example` 负责“具体板子外设怎么接到示例工程”。

## 2. `irq` 模块职责

当前 `irq` 只保留这些公开接口：
- `osal_irq_disable()`
- `osal_irq_enable()`
- `osal_irq_restore()`
- `osal_irq_is_in_isr()`

### 2.1 中断开关语义

- `osal_irq_disable()` 返回的是进入临界区前的原始中断状态快照。
- `osal_irq_enable()` 是无条件开中断。
- `osal_irq_restore(prev_state)` 按进入前状态恢复。

### 2.2 profiling 边界

`irq` 不再直接承载任何 DWT profiling 统计接口。
公开 `osal_irq_*` 只表示中断控制，不再隐式计入采样。

## 3. `cortexm` 模块职责

### 3.1 SysTick / NVIC

`cortexm` 负责这些 OSAL 内核核心依赖：
- CPU 主频配置
- SysTick 时钟源与 tick 频率
- NVIC 优先级分组
- SysTick 优先级
- 原始寄存器映射

对应入口包括：
- `osal_cortexm_init()`
- `osal_cortexm_setup_interrupt_controller()`
- `osal_cortexm_setup_system_tick()`
- `osal_cortexm_get_tick_source()`

### 3.2 DWT profiling backend

DWT profiling 的配置、采样和换算都已经收拢到 `cortexm`：
- `osal_cortexm_profile_init()`
- `osal_cortexm_profile_is_supported()`
- `osal_cortexm_profile_reset()`
- `osal_cortexm_profile_get_stats()`
- `osal_cortexm_profile_cycles_to_ns()`
- `osal_cortexm_profile_cycles_to_us()`

统计结构体为 `osal_cortexm_profile_stats_t`，包含：
- profiling 是否启用
- 当前内核是否支持 DWT 测量
- CPU 主频
- 样本数
- 最近一次、最小、最大、平均 cycle
- 对应的 ns 换算值

### 3.3 只统计 system 内部临界区

当前 DWT 统计不再挂在公开 `osal_irq_disable/restore` 上。
真正参与采样的是 `system/Src` 内部的私有临界区包装：
- `osal_internal_critical_enter()`
- `osal_internal_critical_exit()`

因此现在的统计范围只覆盖 `system` 内核层内部临界区，例如：
- `mem`
- `queue`
- `timer`

外部应用代码即使直接调用 `osal_irq_disable()`，也不会被统计进去。
`components` 层和 `platform/example` 也不在这组采样范围内。

## 4. profiling 开关

profiling 是否启用仍然只受下面这个配置控制：
- `OSAL_CFG_ENABLE_IRQ_PROFILE`

只有当：
- `OSAL_CFG_ENABLE_IRQ_PROFILE != 0`
- 且 `OSAL_CORTEXM_HAS_DWT_CYCCNT != 0`

`osal_init()` 才会在启动阶段配置 DWT 计数器。

## 5. 当前平台支持边界

关于 `DWT CYCCNT`，当前文档边界是：
- `Cortex-M0 / M0+` 默认不支持
- `Cortex-M3 / M4 / M7` 通常支持
- 当前 `STM32F407` 是 `Cortex-M4`，默认按支持处理

如果移植到不支持 DWT 的内核：
- profiling 接口仍然存在
- 但 `osal_cortexm_profile_get_stats()` 会返回“当前不可测量”

## 6. 时间换算

cycle 到时间的换算统一使用：
- `OSAL_CORTEXM_CPU_CLOCK_HZ`

因此：
- `osal_cortexm_profile_cycles_to_ns()`
- `osal_cortexm_profile_cycles_to_us()`

都依赖这项主频配置正确。

## 7. 使用边界

- `irq` 不负责板级外设桥接。
- `cortexm` 不负责 LED / USART / Flash 这类板级示例外设。
- `platform/example` 不负责 OSAL 内核的 SysTick / NVIC / DWT 配置。
- 如果工程只需要中断开关而不需要 profiling，可以直接关闭 `OSAL_CFG_ENABLE_IRQ_PROFILE`。
