# 更新日志

## 2026-04-12

- 收口等待接口状态码语义：
  - 新增 `OSAL_ERR_BLOCKED`
  - 新增 `OSAL_ERR_DELETED`
  - `OSAL_ERR_RESOURCE` 收回为“资源当前不可用且本次不等待”
- `queue / event / mutex` 的阻塞等待路径统一改为：
  - 进入等待时返回 `OSAL_ERR_BLOCKED`
  - 等待超时返回 `OSAL_ERR_TIMEOUT`
  - 等待对象被删除返回 `OSAL_ERR_DELETED`
- `event` 和 `mutex` 已改成和 `queue` 一样的事件驱动等待模型：
  - 等待时挂入等待链表并切到 `BLOCKED`
  - 对象状态变化时主动唤醒等待任务
  - 删除对象时把等待任务带 `OSAL_ERR_DELETED` 唤醒
- `task` 的阻塞原因模型继续完善：
  - 等待原因覆盖 `QUEUE_SEND / QUEUE_RECV / EVENT / MUTEX_LOCK / SLEEP`
  - 恢复结果区分正常唤醒、超时、对象删除
- 示例同步调整：
  - `main.c`
  - `osal_integration_stm32f4.c`
  现在都会显式处理 `OSAL_ERR_BLOCKED` 和 `OSAL_ERR_DELETED`
- 清理一组仅用于 debug 句柄校验的未引用静态辅助函数 warning
- 去掉 `system` 层 `osal_platform_init()` 的 weak 覆写风格，改成显式默认空实现

## 2026-04-11

- 修复并重写一批中文注释，统一为更适合新手阅读的说明风格
- 新增 `docs/` 文档目录，集中放置各模块原理说明
- 补充 `osal.h`、`osal_task.h`、`osal_queue.h` 的总入口与核心接口说明
- 补充 `main.c` 和 `osal_integration_stm32f4.c` 的示例段落注释

## 2026-04-06

- 将 `IRQ` 默认实现切换为 `CMSIS` 风格宏：
  - `__get_IPSR()`
  - `__get_PRIMASK()`
  - `__disable_irq()`
  - `__enable_irq()`
- 将 `SysTick` 和中断控制器的核心配置收回到 `system` 层
- `osal_init()` 自动完成：
  - 中断分组配置
  - `SysTick` 优先级配置
  - `SysTick` 周期 / 使能 / 中断开关配置
- 默认中断配置调整为：
  - 分组：`Group 4`
  - `SysTick`：最低优先级
- `osal_platform_cortexm.*` 明确为模板文件，不参与当前工程编译
- `main.c` 保持“只需包含 `osal.h`”的接入方式

## 2026-03-31

- 明确 `osal_queue_create()` 使用的是 `osal_mem` 管理的统一静态堆
- STM32F4 队列示例整理为更容易复制的形式
- 文档改为中文说明

## 2026-03-30

- OSAL 目录整理为 `system / platform / components` 分层
- `USART` 与 `Flash` 组件进一步独立
- `osal_status_t` 收口到 `osal.h`
- 增加显式 `osal_irq_enable()`
- 保留 `osal_timer_get_tick()` 作为 HAL 风格的毫秒节拍接口

## 2026-03-29

- 初步完成裸机 OSAL 雏形：
  - 任务调度
  - 软件定时器
  - 队列
  - 内存管理
  - 中断抽象
  - USART / Flash 桥接组件
