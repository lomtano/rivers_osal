# OSAL API 能力矩阵

这份文档用于从总览层面说明 OSAL 各模块的调用上下文、资源所有权和调试诊断策略。

## 统一资源契约

- `create / alloc` 成功后，资源所有权归调用方。
- `delete / destroy / free` 成功后，原句柄或指针立即失效，不能再继续使用。
- `delete(NULL) / destroy(NULL) / free(NULL)` 默认视为安全空操作。
- 重复 delete、重复 destroy、陈旧句柄访问属于调用方错误。
- release 构建下，为了保持轻量，OSAL 对部分非法句柄场景会静默返回。
- debug 构建下，只要实现层能检测到重复释放、非法句柄、错误上下文调用、重复绑定等问题，就会通过 `OSAL_DEBUG_HOOK(module, message)` 报告。

## 调试开关

在包含 `osal.h` 之前，可按需定义：

```c
#define OSAL_CFG_ENABLE_DEBUG 1
#define OSAL_DEBUG_HOOK(module, message) \
    printf("[OSAL/%s] %s\r\n", module, message)
#include "osal.h"
```

推荐在 USART 控制台已经挂载后打开该钩子，这样重复绑定、重复 destroy、非法上下文等问题能直接输出到串口日志。

## 模块能力矩阵

### 核心系统层

| 模块 | 接口分类 | 任务态 / 主循环 | ISR | 说明 |
| --- | --- | --- | --- | --- |
| task | create / delete / start / stop / sleep / yield / run | 支持 | 不支持 | 协作式调度，ISR 中调用会返回错误并可触发 debug 诊断 |
| mem | heap init / alloc / free / get_free_size | 支持 | 不建议 | OSAL 统一静态堆 |
| mempool | create / delete | 支持 | 不支持 | 句柄生命周期在任务态管理 |
| mempool | alloc / free | 支持 | 支持 | 适合固定块对象池 |
| irq | disable / restore / enable / is_in_isr | 支持 | 支持 | 由平台层挂接底层实现 |
| timer 基础时基 | get_uptime_us / get_uptime_ms / get_tick | 支持 | 支持 | 由系统层统一完成计时换算和回绕安全差值计算 |
| timer 基础时基 | delay_us / delay_ms | 支持 | 不建议 | ISR 中调用会诊断 |

### 可选系统模块

| 模块 | 接口分类 | 任务态 / 主循环 | ISR | 说明 |
| --- | --- | --- | --- | --- |
| queue | create / create_static / delete | 支持 | 不支持 | 生命周期接口在任务态完成 |
| queue | get_count / send / recv | 支持 | 支持 | 非阻塞接口 |
| queue | send_from_isr / recv_from_isr | 支持 | 支持 | 显式 ISR 友好接口 |
| queue | send_timeout / recv_timeout | 支持 | 不支持 | 内部会让出调度机会 |
| event | create / delete / wait | 支持 | 不支持 | wait 属于阻塞语义 |
| event | set / clear | 支持 | 支持 | 适合中断置位、任务等待 |
| mutex | create / delete / lock / unlock | 支持 | 不支持 | 当前无优先级继承 |
| sw timer | create / start / stop / delete | 支持 | 不支持 | 生命周期接口在任务态完成 |
| sw timer | poll | 支持 | 不支持 | 通常由 `osal_run()` 驱动 |

### 组件层

| 模块 | 接口分类 | 任务态 / 主循环 | ISR | 说明 |
| --- | --- | --- | --- | --- |
| usart | create / destroy / bind_console | 支持 | 不支持 | `bind_console()` 不转移对象所有权 |
| usart | get_console | 支持 | 支持 | 获取当前已绑定控制台后端 |
| usart | write_byte / write / write_string / fputc | 默认支持 | 取决于桥接 | 是否真能在 ISR 中安全使用，取决于底层 SDK 单字节发送实现 |
| flash | create / destroy | 支持 | 不支持 | 桥接对象上下文所有权仍归调用方 |
| flash | unlock / lock / erase / read / write_u8/u16/u32/u64 / write | 默认支持 | 取决于桥接 | 是否允许 ISR 访问，取决于底层 Flash SDK |

## 设计取舍说明

- OSAL 当前以“轻量裸机框架”为目标，因此 release 构建优先保持小开销。
- 对重复释放、非法句柄、错误上下文的诊断能力，采用“debug 可观测、release 尽量轻量”的策略。
- 优先级调度属于“检查顺序优先级”，不是 RTOS 式抢占。
- 低优先级任务有保底扫描机会，但高优先级任务如果长期不返回，仍会拖慢整个系统。

## 推荐实践

- 控制类、短周期、非阻塞任务放高优先级。
- 一般业务处理放中优先级。
- 点灯、心跳、低频诊断类任务放低优先级。
- 串口控制台建议先完成 `osal_platform_uart_create()` 和 `periph_uart_bind_console()`，再打开 `OSAL_CFG_ENABLE_DEBUG` 配套日志输出。
- 对需要在 ISR 与任务之间通信的场景，优先使用 `queue` 或 `event`，不要在 ISR 中直接调用阻塞接口。
