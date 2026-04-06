# 更新日志

## 2026-04-06

- 将 `IRQ` 默认实现切换为 `CMSIS` 风格宏：
  - `__get_IPSR()`
  - `__get_PRIMASK()`
  - `__disable_irq()`
  - `__enable_irq()`
- 将 `SysTick` 和中断控制器的核心配置收回 `system` 层。
- `osal_init()` 现在会自动完成：
  - 中断分组配置
  - `SysTick` 优先级配置
  - `SysTick` 周期 / 使能 / 中断开关配置
- 默认中断配置调整为：
  - 分组：`Group 4`
  - `SysTick`：最低优先级
- `osal_platform_cortexm.*` 明确为模板文件，不参与当前工程编译。
- `main.c` 继续保持只需包含 `osal.h` 的接入方式。
- 合并说明文档，删除冗余 Markdown，保留 `README.md` 和 `CHANGELOG.md` 两份主文档。

## 2026-03-31

- 明确 `osal_queue_create()` 使用的是 `osal_mem` 管理的统一静态堆。
- STM32F4 队列示例整理为更容易复制的形式。
- 文档改为中文说明。

## 2026-03-30

- OSAL 目录整理为 `system / platform / components` 分层。
- `USART` 与 `Flash` 组件进一步独立。
- `osal_status_t` 收口到 `osal.h`。
- 增加显式 `osal_irq_enable()`。
- 保留 `osal_timer_get_tick()` 作为 HAL 风格的毫秒节拍接口。

## 2026-03-29

- 初步完成裸机 OSAL 雏形：
  - 任务调度
  - 软件定时器
  - 队列
  - 内存管理
  - 中断抽象
  - USART / Flash 桥接组件
