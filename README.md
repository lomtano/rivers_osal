# rivers_osal
一个适用于32位MCU裸机的小型系统，旨在快速搭建系统环境

一个面向 **32 位 MCU 裸机场景** 的轻量级 OSAL（Operating System Abstraction Layer）项目。

它的目标不是“做一个完整 RTOS”，而是提供一套足够实用、可移植、可扩展的系统骨架，
让你在 STM32 / GD32 / N32 等平台上，快速搭建任务逻辑与外设适配层。

---

## 项目理念（Philosophy）

### 1) 小而稳（Small but Solid）
- 保持核心机制简单透明，避免过度抽象。
- 优先保障裸机工程里的确定性与可控性。

### 2) 平台无关（Portable by Design）
- 系统能力通过统一 API 暴露。
- 与芯片强相关的部分下沉到 `platform` / `components` 桥接层。

### 3) 先搭框架，再做业务（Framework First）
- 先建立任务、定时、消息通信等“系统骨架”。
- 再在其上堆叠业务模块与外设能力，降低项目早期复杂度。

### 4) 面向 MCU 现实约束（MCU Practicality）
- 不依赖系统堆，使用统一静态内存管理。
- 支持 `create_static` 风格，方便显式控制 RAM 来源。

---

## 仓库能做什么

`rivers_osal` 目前覆盖两大层：

### A. 系统层（System）
位于 `middleware/osal/system`，提供：
- 协作式任务调度（task）
- 固定项消息队列（queue）
- 事件同步（event）
- 互斥量（mutex）
- 软件定时器 + 1us tick 时间基（timer）
- 中断抽象接口（irq）
- 统一静态堆 + 内存池（mem）

### B. 外设组件层（Components）
位于 `middleware/osal/components`，目前包含：
- `periph/usart`：串口桥接组件
- `periph/flash`：Flash 操作桥接组件

组件层的目的，是把“与具体 SDK 绑定的实现”收敛到桥接接口里，
从而让上层业务代码保持稳定。

---

## 代码思路（How It Works）

## 1) 调度思路：协作式执行
主循环反复调用：

```c
while (1) {
    osal_run();
}
```

任务函数应短执行、快返回；需要等待时通过 `sleep/yield` 交还执行权。
这种模型非常适合资源受限且行为可预测的裸机应用。

## 2) 时间思路：1us 统一时间源
平台层提供固定 `1us` 中断，ISR 中调用 `osal_timer_inc_tick()`。
OSAL 内部派生毫秒 tick 与软件定时器超时判定，业务层只使用统一时间 API。

## 3) 通信思路：固定长度泛型队列
队列按 `item_size` 复制消息，可存结构体、指针或定长数组。
在 MCU 场景下，这比临时拼包或全局变量通信更清晰。

## 4) 内存思路：统一入口 + 显式可控
系统对象默认走 `osal_mem`，你也可以用 `create_static` 把关键缓存放在你指定的静态区。
这让内存占用更可估算、更可审计。

## 5) 外设思路：桥接而不是绑定
把 UART / Flash 的硬件细节封装在桥接函数中：
- 上层逻辑不直接依赖 HAL/LL 的细节
- 更换芯片或 SDK 时，主要修改桥接层而不是业务层

---

## 目录结构

```text
.
├── middleware/
│   └── osal/
│       ├── system/            # OSAL 核心能力
│       ├── components/        # 外设桥接组件
│       ├── examples/          # 平台示例（如 stm32f4）
│       ├── PORTING_GUIDE.md   # 移植说明
│       ├── USAGE_EXAMPLES.md  # 使用示例
│       ├── CHANGELOG.md       # 变更日志
│       └── README.md          # OSAL 子模块说明
└── README.md                  # 仓库首页（本文件）
```

---

## 快速开始

1. 阅读模块说明：`middleware/osal/README.md`
2. 按移植指南接入平台：`middleware/osal/PORTING_GUIDE.md`
3. 参考示例完成最小集成：`middleware/osal/examples/stm32f4`
4. 查看使用范式：`middleware/osal/USAGE_EXAMPLES.md`

---

## 适用场景

- 需要“比 super-loop 更结构化”的裸机项目
- 希望保留对内存与执行路径的强控制
- 计划后续扩展到多芯片平台，但不想一开始就上完整 RTOS

---

## 路线建议（Roadmap）

- 增加 host 侧单元测试（mem/queue/timer）
- 完善生命周期与并发约束文档
- 引入更系统的性能与延迟基准
- 逐步沉淀更多可复用组件（如日志、存储抽象、通信协议适配）

---

## License

[MIT](./LICENSE)
