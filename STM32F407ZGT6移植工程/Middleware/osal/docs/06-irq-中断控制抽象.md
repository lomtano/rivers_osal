# irq：中断控制抽象

`irq` 是 `system` 层最薄的一层公共接口。它只负责把 OSAL 需要的中断控制动作统一成一组稳定的包装，避免业务代码直接散落 `__disable_irq()`、`__enable_irq()`、`__get_IPSR()` 这类 CMSIS 原语。

## 1. 模块职责

当前 `irq` 只承担这几件事：

- 提供统一的关中断入口
- 提供统一的开中断入口
- 提供按进入前状态恢复中断的入口
- 提供当前是否处于 ISR 上下文的判断

换句话说，`irq` 解决的是“OSAL 和应用代码要怎样安全地表达中断控制动作”，而不是“Cortex-M 内核外设要怎样配置”。

## 2. 对外接口

当前公开接口只有：

- `osal_irq_disable()`
- `osal_irq_enable()`
- `osal_irq_restore(prev_state)`
- `osal_irq_is_in_isr()`

## 3. 接口语义

### 3.1 `osal_irq_disable()`

- 立即屏蔽可屏蔽中断
- 返回进入临界区前的中断状态快照
- 这个返回值应当配合 `osal_irq_restore(prev_state)` 使用

### 3.2 `osal_irq_enable()`

- 无条件重新开中断
- 适合明确知道当前应该恢复到“开中断”状态的场景

### 3.3 `osal_irq_restore(prev_state)`

- 按进入前的状态恢复中断
- 如果进入前本来就是关中断，就继续保持关中断
- 这是嵌套临界区里更稳妥的恢复方式

### 3.4 `osal_irq_is_in_isr()`

- 用来判断当前调用点是否位于异常/中断上下文
- `queue`、`task`、`timer` 等模块会用它区分“任务态接口”和“ISR 接口”的可用边界

## 4. `irq` 不负责什么

下面这些内容都不属于 `irq`：

- `SysTick` 初始化和 tick 时基配置
- `NVIC Group (4)` 配置
- `DWT` 计数器启用、统计与时间换算
- `LED / USART / Flash` 这类板级外设桥接
- 任何“发送后唤醒等待任务”的调度语义

这些能力现在分别属于：

- `cortexm`：内核外设配置与 DWT profiling backend
- `platform/example`：板级示例桥接
- `task / queue / timer / mem`：OSAL 内核本体

## 5. 与 DWT profiling 的边界

当前 `irq` 不再直接承载任何 DWT profiling 统计接口。

也就是说：

- 公开 `osal_irq_*` API 只表示“中断控制动作”
- 外部代码直接调用 `osal_irq_disable()` / `osal_irq_restore()`，不会被自动计入 profiling
- profiling 的后端配置、统计结构体和 `cycles -> ns/us` 换算都已经收口到 `cortexm`

这样做的目的，是把“中断控制抽象”和“内核性能测量后端”彻底分开，避免职责混在一起。

## 6. 当前实际使用方式

`system` 层内部需要做短临界区保护时，会在 `mem / queue / timer` 这些实现里调用 `osal_irq_*`。

因此 `irq` 的角色更像一层统一门面：

- 对内：给 OSAL 内核实现提供统一的中断控制原语
- 对外：给应用层保留最小、最可预测的中断控制接口

## 7. 使用边界

- 临界区应尽量短，只包真正需要原子性的那几行代码
- 优先用 `disable + restore` 成对出现，而不是到处直接 `enable`
- 不要把长时间轮询、串口输出、Flash 操作放进 `irq` 临界区
- 如果你只是想看 OSAL 内核临界区耗时，应打开 `DWT profiling`，而不是依赖 `irq` 自己做统计
