 # rivers_osal
-一个适用于32位MCU裸机的小型系统，旨在快速搭建系统环境
 
-轻量级 MCU 裸机 OSAL（Operating System Abstraction Layer）工程。
+一个面向 32 位 MCU 裸机项目的轻量 OSAL（Operating System Abstraction Layer）工程，目标是：
+**在不引入 RTOS 的前提下，快速搭建可维护、可移植的工程运行骨架。**
 
-> 面向 STM32 / GD32 / N32 等 32-bit MCU，提供一套“协作式调度 + 时间基 + 消息通信 + 外设桥接”的可移植骨架。
+> 当前定位：**Keil（MDK-ARM）工程优先**，直接编译生成固件（如 `.hex` / `.axf`）。  
+> 说明：仓库当前不以 CMake 为主流程，这是有意设计，便于团队直接沿用 Keil 工程发布固件。
 
 ---
 
-## 项目目标
+## 设计目标
 
-`rivers_osal` 的目标不是实现完整 RTOS，而是提供可快速落地的中间层：
+`rivers_osal` 并不是完整 RTOS，而是“裸机可控 + 工程化抽象”之间的平衡层：
 
-- 用统一 API 组织任务、队列、事件、互斥、定时器与内存。
-- 用组件桥接隔离芯片 HAL/SDK 差异。
-- 保留裸机项目的可控性（内存、执行路径、时延行为）。
+- 用统一 API 组织任务、队列、事件、互斥量、定时器与内存。
+- 通过 platform/components 隔离 MCU HAL/SDK 差异。
+- 保持裸机项目对内存占用、执行路径、时延行为的可控性。
 
 ---
 
-## 当前代码实现的关键事实
+## 系统特性（当前实现）
 
-### 1) 任务调度：协作式（非抢占）
-- 任务状态机：`READY / RUNNING / BLOCKED / SUSPENDED`。
-- `osal_run()` 每轮扫描任务链表并执行 `READY` 任务。
-- 任务不切栈、不抢占，任务函数应短执行并主动 `yield/sleep`。
+### 1) 协作式调度（非抢占）
 
-### 2) 时间模型：1us tick 驱动
-- 平台层中断每 1us 调用 `osal_timer_inc_tick()`。
-- OSAL 内部维护 us/ms 时间与软件定时器。
-- `osal_timer_poll()` 在 `osal_run()` / `osal_task_yield()` 中被调用。
+- 调度模型是**协作式**，不做抢占式上下文切换。
+- 提供高/中/低三档优先级，采用“检查顺序优先级”。
+- 低优先级存在保底扫描机会，避免长期完全饥饿。
 
-### 3) 队列模型：固定项大小环形队列
-- 支持 `create`（走 `osal_mem`）与 `create_static`（用户缓冲区）。
-- 支持非阻塞、超时等待、ISR 场景接口。
+> 说明：裸机场景下，协作式是有意选择；任务需保持短执行并主动 `sleep/yield`。
 
-### 4) 内存模型：统一静态堆 + 可选内存池
-- `osal_mem` 使用 first-fit + 相邻块合并。
-- `osal_mempool` 用固定块 free-list，适合稳定对象分配。
+### 2) 时间基与软件定时器
 
-### 5) 外设桥接：UART / Flash
-- `periph_uart`：最小桥接接口为 `write_byte`，可绑定 `fputc` 控制台。
-- `periph_flash`：统一擦写读接口，支持按对齐宽度（8/16/32/64）自动选择写入路径。
+- 系统通过 `osal_tick_handler()` 接入周期时基中断。
+- 支持 us/ms 级时间读取、延时、软件定时器（单次/周期）。
+- 软件定时器由 `osal_run()` 主循环轮询驱动。
+
+### 3) 通信与同步
+
+- 固定项大小队列（支持动态/静态创建）。
+- 事件、互斥量。
+- 提供 ISR 友好接口（例如 queue/event 的部分接口）。
+
+### 4) 内存管理
+
+- `osal_mem`：统一静态堆管理（first-fit + 相邻块合并）。
+- `osal_mempool`：固定块对象池，适合稳定对象分配场景。
+
+### 5) 外设桥接组件
+
+- USART 组件：最小桥接接口为单字节发送。
+- Flash 组件：统一擦写读接口，支持多写宽路径。
 
 ---
 
-## 仓库结构
+## 仓库结构（当前）
 
 ```text
 .
-├── middleware/
-│   └── osal/
-│       ├── system/
-│       │   ├── Inc/                  # OSAL 核心头文件
-│       │   └── Src/                  # OSAL 核心实现
-│       ├── components/
-│       │   └── periph/
-│       │       ├── usart/            # UART 桥接组件
-│       │       └── flash/            # Flash 桥接组件
-│       ├── examples/
-│       │   └── stm32f4/              # STM32F4 集成示例
-│       ├── PORTING_GUIDE.md
-│       ├── USAGE_EXAMPLES.md
-│       ├── CHANGELOG.md
-│       ├── DEEP_DIVE.md
-│       └── README.md                 # OSAL 子模块文档
+├── osal/                                   # OSAL 主体（建议复用的中间件目录）
+│   ├── system/                             # 核心：task/queue/event/mutex/timer/mem/irq
+│   ├── platform/                           # 平台适配模板 + STM32F4 示例
+│   ├── components/                         # 可复用组件（USART/Flash/RTT/KEY）
+│   ├── README.md
+│   ├── PORTING_GUIDE.md
+│   ├── USAGE_EXAMPLES.md
+│   └── API_CAPABILITY_TABLE.md
+├── STM32F407ZGT6移植工程/                  # 可直接在 Keil 打开的参考工程
+│   ├── MDK-ARM/rivers_osal.uvprojx
+│   ├── Core/
+│   ├── Drivers/
+│   └── Middleware/
 ├── LICENSE
 └── README.md
 ```
 
 ---
 
-## 运行路径（最小理解）
-
-```mermaid
-sequenceDiagram
-    autonumber
-    participant MAIN as main()
-    participant TICK as 1us IRQ
-    participant OSAL as osal_run()
-    participant TASK as task_fn()
-
-    MAIN->>MAIN: platform init
-    MAIN->>MAIN: create/start tasks
-
-    loop forever
-        TICK->>OSAL: osal_timer_inc_tick()
-        MAIN->>OSAL: osal_run()
-        OSAL->>OSAL: osal_timer_poll()
-        OSAL->>TASK: run READY task
-        TASK-->>OSAL: return / yield / sleep
-    end
-```
+## 快速开始（Keil 工作流）
 
----
+### A. 直接运行参考工程
+
+1. 使用 Keil 打开：
+   - `STM32F407ZGT6移植工程/MDK-ARM/rivers_osal.uvprojx`
+2. 编译后生成 `.axf/.hex` 并下载。
+3. 主循环中保持：
 
-## 快速开始（建议顺序）
+```c
+while (1) {
+    osal_run();
+}
+```
 
-1. 阅读 `middleware/osal/README.md`（子系统总览）。
-2. 阅读 `middleware/osal/PORTING_GUIDE.md`（平台移植要点）。
-3. 参考 `middleware/osal/examples/stm32f4/osal_integration_stm32f4.c`（完整主循环接入）。
-4. 按 `middleware/osal/USAGE_EXAMPLES.md` 组织自己的任务与队列。
+### B. 集成到你自己的裸机工程
 
----
+1. 按 `osal/PORTING_GUIDE.md` 复制目录并加入 include/src。
+2. 完成平台层桥接（tick source / irq / uart / flash）。
+3. 在系统时基中断中调用（Cube/HAL 工程示例）：
+
+```c
+void SysTick_Handler(void)
+{
+    HAL_IncTick();   // 或你的 SDK tick handler
+    osal_tick_handler();
+}
+```
 
-## 接入示例（节选）
+4. 在 `main()` 完成初始化：
 
 ```c
 int main(void)
 {
-    platform_init();
-    osal_platform_init();
-    osal_platform_tick_start();
+    board_init();
+    osal_init();
 
     app_create_tasks();
 
     while (1) {
         osal_run();
     }
 }
 ```
 
 ---
 
-## 适用场景
+## 推荐适用场景
 
-- 希望从 super-loop 过渡到更结构化的裸机架构。
-- 需要跨平台迁移，但不想过早绑定某 RTOS 生态。
-- 希望把 HAL 差异限制在桥接层，而不是散落在业务代码。
+- 从传统 super-loop 向“有任务组织的裸机架构”演进。
+- 需要跨芯片迁移，但暂不引入 RTOS。
+- 希望把 HAL 差异收敛到平台桥接层，而不是散落在业务代码。
 
 ---
 
-## 后续可演进方向
+## 文档索引
 
-- 引入可选优先级调度策略（在保持现有协作模型基础上兼容）。
-- 增加 host 侧单元测试（queue/mem/timer）。
-- 补充性能指标（最大调度周期、queue 延迟、timer 漂移）。
+- OSAL 总览：`osal/README.md`
+- 移植指南：`osal/PORTING_GUIDE.md`
+- 使用示例：`osal/USAGE_EXAMPLES.md`
+- API 能力矩阵：`osal/API_CAPABILITY_TABLE.md`
+- 更新日志：`osal/CHANGELOG.md`
 
 ---
 
 ## License
 
 [MIT](./LICENSE)
