# 更新日志

## 2026-03-30

### 内存与队列

- 明确 `osal_queue_create()` 使用的是 `osal_mem` 管理的 OSAL 静态堆，而不是系统 `heap`
- STM32F4 队列示例切换为 `osal_queue_create_static()`，默认使用显式静态数组作为消息缓存区
- 进一步强调队列支持结构体、指针和定长数组等任意固定大小消息类型

### STM32F4 适配模板

- 把 `osal_platform_stm32f4.h` 整理成更适合移植复用的模板骨架
- 新增 `TIMx` 相关宏配置，便于替换定时器实例、中断号和 APB 总线归属
- 新增 `osal_platform_systick_handler()`，用于需要挂接 `SysTick` 的工程
- 保留 `osal_timer_inc_tick()` 作为唯一的 OSAL 计时入口，保持和 `HAL_IncTick()` 相似的使用体验

### 文档

- 将 `README.md`、`PORTING_GUIDE.md`、`USAGE_EXAMPLES.md` 改为中文说明
- 将组件层相关 `README.md` 改为中文说明
- 补充了 OSAL 静态内存模型、`TIMx/SysTick` 接法和静态队列推荐用法

## 2026-03-29

### 目录结构

- 将 OSAL 目录整理为 `system/`、`components/`、`examples/`
- 增加 `components/periph/` 这一层，让外设组件与未来的 `rtt/`、`bootloader/` 等小组件保持统一结构
- 将 `USART` 调整到 `middleware/osal/components/periph/usart/`
- 保留 `Flash` 于 `middleware/osal/components/periph/flash/`

### 核心接口

- 删除 `osal_status.h`，并把 `osal_status_t` 收口到 `osal.h`
- 增加显式 `osal_irq_enable()`
- 将定时器模型简化为固定 `1us` 中断中调用一次 `osal_timer_inc_tick()`
- 保留 `osal_timer_get_tick()` 作为 HAL 风格的回绕毫秒计数

### 队列

- 将 `osal_queue` 重构为泛型固定成员大小队列接口
- 增加基于 OSAL 静态堆的 `osal_queue_create(length, item_size)`
- 增加基于用户缓冲区的 `osal_queue_create_static(buffer, length, item_size)`
- 明确支持结构体、指针和定长数组

### 组件

- 保留基于“发送单字节回调”的 USART 桥接接口
- 保留能按写入宽度自动选择实现的 Flash 桥接接口

### 示例与文档

- 更新 STM32F4 集成示例，改为结构体消息队列演示
- 更新移植指南，适配新的组件层级
- 更新使用说明，匹配新的队列和定时器行为
