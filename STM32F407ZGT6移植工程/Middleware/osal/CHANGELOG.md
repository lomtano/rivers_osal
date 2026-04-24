# 更新日志

## 2026-04-23

- 将 OSAL 收口为纯协作式裸机内核。
- 新增统一配置头 `system/Inc/osal_config.h`。
- `osal.h` 不再承载功能开关定义，只保留状态码、调试宏和模块聚合。
- 删除 `event`、`mutex` 模块的公开接口、源码与工程编译引用。
- `task` 删除伪阻塞语义，只保留 `create / delete / start / stop / yield / run`。
- `queue` 统一为 `send/recv(timeout_ms)`：
  - `0U` 为立即尝试
  - `N` 为基于系统 tick 的同步忙等重试
- 删除 `create_static`、独立 `send_timeout/recv_timeout` 接口和等待链表实现。
- `irq/platform` 重新划分 DWT profiling 边界，profiling 是否启用只受 `OSAL_CFG_ENABLE_IRQ_PROFILE` 控制。
- 同步更新 `main.c`、`osal_integration_stm32f4.c` 和核心文档。

## 2026-04-12

- 收口过渡版本的 system/platform 边界。
- 整理一轮状态码、注释和示例说明，为后续核心简化做准备。

## 2026-04-11

- 新增 `docs/` 文档目录。
- 统一一批中文注释风格与示例说明。

## 2026-04-06

- 将 SysTick 与中断控制器的核心配置收回到 `system` 层。
- `osal_init()` 自动完成中断分组、SysTick 优先级和 SysTick 启动配置。

## 2026-03-31

- 明确 `osal_queue_create()` 使用统一静态堆。
- STM32F4 队列示例整理为更容易复制的形式。

## 2026-03-30

- OSAL 目录整理为 `system / platform / components` 分层。
- `USART` 与 `Flash` 组件进一步独立。

## 2026-03-29

- 初步完成裸机 OSAL 雏形：任务调度、软件定时器、队列、内存管理、中断抽象和外围组件桥接。
