# 更新日志

## 2026-04-12

- 将 `event` 的等待逻辑从“循环检查 + yield”改为和 `queue` 一致的事件驱动等待模型：
  - 等待时把当前任务挂到事件等待链表
  - 任务状态切到 `BLOCKED`
  - `osal_event_set()` 直接唤醒等待任务
  - `osal_event_delete()` 会把等待任务带 `OSAL_ERR_RESOURCE` 唤醒
- 将 `mutex` 的等待逻辑从“尝试加锁 + yield 重试”改为和 `queue` 一致的事件驱动等待模型：
  - 拿不到锁时把当前任务挂到互斥量等待链表
  - 任务状态切到 `BLOCKED`
  - `osal_mutex_unlock()` 直接唤醒一个等待任务
  - `osal_mutex_delete()` 会把等待任务带 `OSAL_ERR_RESOURCE` 唤醒
- 扩展 `task` 内部等待原因，新增：
  - `OSAL_TASK_WAIT_EVENT`
  - `OSAL_TASK_WAIT_MUTEX_LOCK`
- 补齐 `task` 内部阻塞恢复链路：
  - 超时路径会从事件/互斥量等待链表中摘除任务
  - `stop/delete` 路径会先解除事件/互斥量等待对象关联
  - 恢复结果继续统一通过任务控制块中的 `resume_*` 字段回传
- 同步调整示例代码，避免“进入 `BLOCKED` 后又继续执行 `sleep` 覆盖等待状态”的错误写法：
  - `main.c`
  - `osal_integration_stm32f4.c`
- 继续补充 `event / mutex / task` 相关实现注释，把等待链表、阻塞恢复、超时处理等细节解释完整。

## 2026-04-11

- 修复并重写一批中文注释，统一为更适合新手阅读的说明风格。
- 新增 `docs/` 文档目录，集中放置各模块原理说明。
- 补充 `osal.h`、`osal_task.h`、`osal_queue.h` 的总入口与核心接口说明。
- 补充 `main.c` 和 `osal_integration_stm32f4.c` 的示例段落注释。

## 2026-04-06

- 将 `IRQ` 默认实现切换为 `CMSIS` 风格宏：
  - `__get_IPSR()`
  - `__get_PRIMASK()`
  - `__disable_irq()`
  - `__enable_irq()`
- 将 `SysTick` 和中断控制器的核心配置收回到 `system` 层。
- `osal_init()` 自动完成：
  - 中断分组配置
  - `SysTick` 优先级配置
  - `SysTick` 周期 / 使能 / 中断开关配置
- 默认中断配置调整为：
  - 分组：`Group 4`
  - `SysTick`：最低优先级
- `osal_platform_cortexm.*` 明确为模板文件，不参与当前工程编译。
- `main.c` 保持“只需包含 `osal.h`”的接入方式。

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
