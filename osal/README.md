# OSAL 中间件

`Middleware/osal` 是一套面向 32 位 MCU 的轻量裸机 OSAL 骨架。  
它的目标不是复制完整 RTOS，而是提供一套足够小、足够清晰、又方便跨芯片移植的逻辑框架。

## 目录结构

```text
Middleware/osal/
|-- system/
|   |-- Inc/
|   `-- Src/
|-- platform/
|   |-- osal_platform_cortexm.h
|   |-- osal_platform_cortexm.c
|   `-- example/
|       `-- stm32f4/
|           |-- osal_platform_stm32f4.h
|           |-- osal_platform_stm32f4.c
|           `-- osal_integration_stm32f4.c
|-- components/
|   |-- periph/
|   |   |-- usart/
|   |   `-- flash/
|   `-- README.md
|-- README.md
|-- PORTING_GUIDE.md
|-- USAGE_EXAMPLES.md
`-- CHANGELOG.md
```

## 分层说明

- `system/`
  OSAL 系统层，提供任务、队列、事件、互斥量、内存管理、中断抽象和定时器。

- `platform/`
  平台适配层，负责把 OSAL 和具体 MCU SDK 接起来。

- `platform/osal_platform_cortexm.*`
  通用 Cortex-M 适配模板。主要告诉用户：哪些宏和桥接函数需要填写。

- `platform/example/stm32f4/osal_platform_stm32f4.*`
  按模板填写出的 STM32F4 实际适配示例。

- `platform/example/stm32f4/osal_integration_stm32f4.c`
  OSAL 功能使用示例集，演示任务、事件、互斥量、队列、软件定时器、USART 和 Flash 组件怎么用。

- `components/`
  可复用的小组件层。目前提供 `periph/usart` 和 `periph/flash`。

## 当前能力

- 协作式任务调度
- 高/中/低三档调度优先级
- 泛型固定成员大小消息队列
- 事件与互斥量
- `osal_mem` 统一静态内存管理
- 基于系统时基原始读接口实现的 `tick/us delay`
- 单次和周期性软件定时器
- 中断开关与 ISR 上下文判断
- 基于单字节发送桥接的 USART 组件
- 支持不同擦写粒度和写入宽度桥接的 Flash 组件

## 设计原则

- `system` 负责复杂逻辑，`platform` 只做桥接
- OSAL 不直接依赖具体 MCU SDK 头文件
- 裸机下的优先级不是抢占式，而是“检查顺序优先级”
- 示例文件只做参考，不强行混进正式应用层

## 文档

- 移植步骤：`Middleware/osal/PORTING_GUIDE.md`
- 使用示例：`Middleware/osal/USAGE_EXAMPLES.md`
- 更新日志：`Middleware/osal/CHANGELOG.md`
