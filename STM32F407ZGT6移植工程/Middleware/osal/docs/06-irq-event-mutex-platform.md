# irq / event / mutex / platform

## 1. irq：中断抽象

### 模块职责

`osal_irq` 负责把：

- 中断关闭
- 中断打开
- 中断恢复
- 当前是否处于 ISR

统一成 OSAL 内核使用的入口。

对应文件：

- [osal_irq.h](/abs/path/A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal/system/Inc/osal_irq.h)
- [osal_irq.c](/abs/path/A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal/system/Src/osal_irq.c)

### 实现特点

它本身不直接绑定 STM32 HAL，而是依赖：

- `OSAL_PLATFORM_IRQ_GET_IPSR()`
- `OSAL_PLATFORM_IRQ_GET_PRIMASK()`
- `OSAL_PLATFORM_IRQ_RAW_DISABLE()`
- `OSAL_PLATFORM_IRQ_RAW_ENABLE()`

这些宏来自 [osal_platform.h](/abs/path/A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal/system/Inc/osal_platform.h)。

### 这意味着什么

- system 层不依赖某一家厂商 SDK
- 只依赖 Cortex-M 风格的 CMSIS 能力
- 真正的架构绑定由 platform 宏完成

## 2. event：轻量事件对象

### 模块职责

`osal_event` 提供：

- `set`
- `clear`
- `wait`

并支持 `auto_reset` 语义。

### 当前实现方式

当前 `wait` 不是队列那种事件驱动阻塞，而是：

1. 检查事件状态
2. 不满足则 `yield`
3. 再次回来继续检查

所以它本质上是：

- 轮询
- 但会主动让出执行权

### 适合什么场景

适合：

- 简单同步
- 低复杂度事件通知

不适合：

- 高竞争
- 大量任务等待同一事件
- 对调度效率敏感的场景

## 3. mutex：最小互斥实现

### 模块职责

`osal_mutex` 提供最小互斥量：

- `create`
- `lock`
- `unlock`

### 当前实现方式

当前 `lock` 大体是：

1. 进临界区检查 `locked`
2. 如果能拿到锁就返回
3. 否则 `yield`
4. 再回来重试

### 当前限制

这不是 RTOS 级互斥量，因为它没有：

- owner 记录
- 优先级继承
- 等待队列

所以它的定位应该是：

- 轻量资源保护
- 低冲突使用

而不是复杂实时互斥场景。

## 4. platform：system 层如何接管 SysTick 和 NVIC

### 模块职责

`osal_platform` 是 system 内部的架构抽象层。

它负责：

- 配置中断分组
- 配置 SysTick 优先级
- 配置 SysTick 时钟源
- 配置 SysTick 重装值
- 暴露原始 tick source 给 timer 模块

对应文件：

- [osal_platform.h](/abs/path/A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal/system/Inc/osal_platform.h)
- [osal_platform.c](/abs/path/A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal/system/Src/osal_platform.c)

### 当前设计理念

原则是：

- 跟 `SysTick/NVIC/IRQ` 有关的核心机制，放在 system
- 跟具体 UART/Flash/LED/SDK 有关的桥接，放在 `platform/example/<board>`

### 为什么要这么分

因为：

- `SysTick/NVIC` 是 OSAL 内核正常工作的必要部分
- 不是“某个外设组件”
- 应该由内核自己掌控

反过来：

- 串口、Flash、LED 不属于内核最小闭环
- 这些桥接放板级适配更合理

## 5. platform/example/stm32f4 的定位

当前具体板级适配文件是：

- [osal_platform_stm32f4.h](/abs/path/A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal/platform/example/stm32f4/osal_platform_stm32f4.h)
- [osal_platform_stm32f4.c](/abs/path/A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal/platform/example/stm32f4/osal_platform_stm32f4.c)

它负责：

- USART bridge
- Flash bridge
- LED 示例桥接

它不应该负责：

- 内核 SysTick 主逻辑
- 内核调度逻辑
- 内存管理逻辑

## 6. 当前边界的价值

现在这套边界的价值在于：

- OSAL system 层不需要知道 `HAL_UART_Transmit`
- 也不需要知道某家 Flash SDK API 名字
- 以后换 STM32/GD32/N32 时，主要改 platform/example

## 7. 仍需明确写进文档的限制

文档里应明确：

- 这不是完全架构无关的抽象
- 它依赖 Cortex-M 的 `SysTick` 和 `NVIC`
- 对非 Cortex-M，必须先改 `osal_platform`

否则读者容易误以为它可以零改动搬到任意 32 位 MCU。
