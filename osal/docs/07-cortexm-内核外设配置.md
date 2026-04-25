# cortexm：内核外设配置

`cortexm` 这一层负责 OSAL 真正依赖的 Cortex-M 内核外设配置。它不再承担通用 `irq` 抽象，也不负责 LED、USART、Flash 这类板级桥接，而是专门处理：

- `SysTick`
- `NVIC Group (4)` 和 `SysTick` 优先级
- `DWT CYCCNT`
- 未来 `MPU` 的预留位置

这篇文档不只解释“做了什么”，还把当前代码真正用到的寄存器、地址、位定义和配置顺序写出来，方便移植和排错。

## 职责与接口总览

`cortexm` 在 OSAL 中的作用是：把 OSAL 内核真正依赖的 Cortex-M 内核外设配置集中到一个地方。它服务于 `timer`、`irq` 和 profiling，不服务于 LED、USART、Flash 这类板级外设。

对外接口主要包括：

- `osal_cortexm_init()`：平台初始化钩子，当前默认是空实现。
- `osal_cortexm_setup_interrupt_controller()`：配置 NVIC 优先级分组和 SysTick 优先级。
- `osal_cortexm_setup_system_tick()`：配置并启动 SysTick。
- `osal_cortexm_get_tick_source()`：把 SysTick 读数能力提供给 `timer` 层。
- `osal_cortexm_profile_init()`：初始化 DWT CYCCNT profiling 后端。
- `osal_cortexm_profile_get_stats()`：读取临界区 profiling 统计。
- `osal_cortexm_profile_cycles_to_ns()` / `osal_cortexm_profile_cycles_to_us()`：把 cycle 换算成时间。

软件思路是：`timer` 层只关心“时基怎么读”，`irq` 层只关心“中断怎么关/恢复”，真正写 Cortex-M 寄存器的代码统一留在 `cortexm` 层。

## 1. 实际初始化顺序

当前 `osal_init()` 的真实顺序在 `system/Src/osal_timer.c` 里是：

1. `osal_cortexm_init()`
2. `osal_cortexm_setup_interrupt_controller()`
3. `osal_cortexm_profile_init()`
4. `osal_cortexm_setup_system_tick()`
5. `osal_timer_sync_tick_source()`

也就是说，当前 OSAL 会先处理优先级分组和 SysTick 优先级，再准备 DWT profiling，最后真正启动 `SysTick`。

## 2. SysTick

### 2.1 当前用到的寄存器

| 寄存器 | 地址 | 当前代码里的宏 | 用途 |
| --- | --- | --- | --- |
| `SysTick CTRL` | `0xE000E010` | `OSAL_CORTEXM_SYSTICK_CTRL_REG` | 选择时钟源、打开中断、打开计数器、读取 `COUNTFLAG` |
| `SysTick LOAD` | `0xE000E014` | `OSAL_CORTEXM_SYSTICK_LOAD_REG` | 写入重装值 |
| `SysTick VAL` | `0xE000E018` | `OSAL_CORTEXM_SYSTICK_CURRENT_VALUE_REG` | 读取当前倒计数值，写任意值清零当前计数 |

### 2.2 当前用到的位

| 位 | 宏 | 含义 |
| --- | --- | --- |
| `CTRL[2]` | `OSAL_CORTEXM_SYSTICK_CLK_BIT` | `1` 表示用内核时钟 `HCLK`，`0` 表示外部分频时钟 |
| `CTRL[1]` | `OSAL_CORTEXM_SYSTICK_INT_BIT` | 允许计数到 `0` 时触发 `SysTick` 中断 |
| `CTRL[0]` | `OSAL_CORTEXM_SYSTICK_ENABLE_BIT` | 打开 `SysTick` 计数器 |
| `CTRL[16]` | `OSAL_CORTEXM_SYSTICK_COUNTFLAG_BIT` | 只读回卷标志，表示自上次读取后至少发生过一次计数到 `0` |

### 2.3 当前配置宏

这几个宏决定 `SysTick` 怎么配：

- `OSAL_CORTEXM_CPU_CLOCK_HZ`
- `OSAL_CORTEXM_SYSTICK_CLOCK_HZ`
- `OSAL_CORTEXM_TICK_RATE_HZ`
- `OSAL_CORTEXM_SYSTICK_USE_CORE_CLOCK`

默认含义是：

- CPU 主频 `168 MHz`
- `SysTick` 输入时钟默认等于 CPU 主频
- OSAL tick 频率默认 `1000 Hz`
- 默认使用内核时钟 `HCLK`

### 2.4 实际配置过程

`osal_cortexm_setup_system_tick()` 现在按下面这个顺序工作：

1. 检查 `OSAL_CORTEXM_SYSTICK_CLOCK_HZ` 和 `OSAL_CORTEXM_TICK_RATE_HZ` 是否为 `0`
2. 计算 `reload_value = systick_clock_hz / tick_rate_hz`
3. 检查这个值是否落在 `SysTick` 的 24 位计数范围内
4. 组合 `CTRL` 要写入的位：
   - 一定打开 `TICKINT`
   - 一定打开 `ENABLE`
   - 如果 `OSAL_CORTEXM_SYSTICK_USE_CORE_CLOCK != 0`，再打开 `CLKSOURCE`
5. 先把 `CTRL` 写 `0`，确保修改期间不会带着旧配置运行
6. 把 `VAL` 写 `0`，清掉当前计数状态
7. 把 `LOAD` 写成 `reload_value - 1`
8. 最后一次性把组合好的 `CTRL` 写回去

这里有两个关键点：

- `LOAD` 里写的是 `周期计数值 - 1`，因为 `SysTick` 实际周期长度是 `LOAD + 1`
- 必须先停表、再清当前值、再写 `LOAD`、最后开表，这样启动状态最确定

### 2.5 现在为什么还要暴露 tick source

`timer` 模块不会自己直接写 `SysTick` 寄存器，它通过 `osal_cortexm_get_tick_source()` 拿到一组读取函数：

- 读输入时钟频率
- 读 `LOAD`
- 读当前值
- 判断 `SysTick` 是否已使能
- 判断 `COUNTFLAG` 是否出现过回卷

这样 `timer` 层只关心“时基怎么读”，不直接依赖 `SysTick` 寄存器名字。

## 3. NVIC Group (4) 与 SysTick 优先级

### 3.1 当前用到的寄存器

| 寄存器 | 地址 | 当前代码里的宏 | 用途 |
| --- | --- | --- | --- |
| `SCB AIRCR` | `0xE000ED0C` | `OSAL_CORTEXM_SCB_AIRCR_REG` | 配置 `PRIGROUP` |
| `SCB SHPR3` | `0xE000ED20` | `OSAL_CORTEXM_SCB_SHPR3_REG` | 配置 `SysTick` 的系统异常优先级 |

### 3.2 当前用到的位域

#### `AIRCR`

| 位域 | 宏 | 含义 |
| --- | --- | --- |
| `[31:16]` | `OSAL_CORTEXM_AIRCR_VECTKEY` / `..._POS` / `..._MASK` | 写 `AIRCR` 时必须同时写入钥匙值 `0x5FA` |
| `[10:8]` | `OSAL_CORTEXM_AIRCR_PRIGROUP_POS` / `..._MASK` | 中断优先级分组 |

#### `SHPR3`

| 位域 | 宏 | 含义 |
| --- | --- | --- |
| `[31:24]` | `OSAL_CORTEXM_SHPR3_SYSTICK_POS` / `..._MASK` | `SysTick` 优先级字段 |

### 3.3 当前配置宏

- `OSAL_CORTEXM_CONFIGURE_PRIORITY_GROUP`
- `OSAL_CORTEXM_NVIC_PRIORITY_GROUP_RAW`
- `OSAL_CORTEXM_CONFIGURE_SYSTICK_PRIORITY`
- `OSAL_CORTEXM_NVIC_PRIO_BITS`
- `OSAL_CORTEXM_SYSTICK_PRIORITY_LEVEL`

默认值的含义是：

- 自动配置优先级分组
- `PRIGROUP` 原始值为 `3`
- 自动配置 `SysTick` 优先级
- 内核实现了 `4` 位优先级位
- `SysTick` 放到最低优先级

对当前 STM32F4 这套配置来说，目标就是常说的 `NVIC Group 4`。

### 3.4 `AIRCR` 的实际写法

`osal_cortexm_configure_priority_group()` 的过程是：

1. 检查 `OSAL_CORTEXM_NVIC_PRIORITY_GROUP_RAW` 是否在 `0..7`
2. 先读出当前 `AIRCR`
3. 清掉原来的 `VECTKEY` 和 `PRIGROUP`
4. 把 `0x5FA` 写回 `VECTKEY`
5. 把新的 `PRIGROUP` 写进 `[10:8]`
6. 整个值再写回 `AIRCR`

这里最容易错的地方就是：

- `AIRCR` 不是普通寄存器
- 只改 `PRIGROUP` 不带 `VECTKEY`，硬件会忽略这次写入

### 3.5 `SHPR3` 的实际写法

`osal_cortexm_configure_systick_priority()` 的过程是：

1. 先检查 `OSAL_CORTEXM_NVIC_PRIO_BITS` 是否落在 `1..8`
2. 再检查 `OSAL_CORTEXM_SYSTICK_PRIORITY_LEVEL` 是否超出可编码范围
3. 把逻辑优先级编码成高位有效的 8 位字段：
   - `encoded_priority = level << (8 - prio_bits)`
4. 读出当前 `SHPR3`
5. 清掉 `SysTick` 对应的 `[31:24]`
6. 把编码后的优先级写回 `[31:24]`

这里的关键点是：

- Cortex-M 的优先级字段不是低位有效
- 真正实现的优先级位数取决于内核
- 所以写 `SHPR3` 前必须先按 `NVIC_PRIO_BITS` 做左移编码

## 4. DWT CYCCNT

### 4.1 当前用到的寄存器

| 寄存器 | 地址 | 当前代码里的宏 | 用途 |
| --- | --- | --- | --- |
| `SCB DEMCR` | `0xE000EDFC` | `OSAL_CORTEXM_SCB_DEMCR_REG` | 打开 CoreSight trace 总开关 |
| `DWT CTRL` | `0xE0001000` | `OSAL_CORTEXM_DWT_CTRL_REG` | 使能 `CYCCNT` |
| `DWT CYCCNT` | `0xE0001004` | `OSAL_CORTEXM_DWT_CYCCNT_REG` | 读取周期计数 |
| `DWT LAR` | `0xE0001FB0` | `OSAL_CORTEXM_DWT_LAR_REG` | 仅在部分内核实现中需要解锁 |

### 4.2 当前用到的位

| 位 | 宏 | 含义 |
| --- | --- | --- |
| `DEMCR[24]` | `OSAL_CORTEXM_SCB_DEMCR_TRCENA_BIT` | 打开 trace / debug 相关功能 |
| `DWT_CTRL[0]` | `OSAL_CORTEXM_DWT_CTRL_CYCCNTENA_BIT` | 允许 `CYCCNT` 开始计数 |

如果平台声明 `OSAL_CORTEXM_DWT_HAS_LAR = 1`，还会额外向 `DWT LAR` 写入解锁值：

- `0xC5ACCE55`

### 4.3 当前配置宏

- `OSAL_CFG_ENABLE_IRQ_PROFILE`
- `OSAL_CORTEXM_HAS_DWT_CYCCNT`
- `OSAL_CORTEXM_DWT_HAS_LAR`
- `OSAL_CORTEXM_CPU_CLOCK_HZ`

### 4.4 实际启用过程

`osal_cortexm_profile_init()` 现在按下面这个顺序工作：

1. 先关中断，避免初始化统计状态时被并发打断
2. 只有在下面两个条件同时满足时才继续：
   - `OSAL_CFG_ENABLE_IRQ_PROFILE != 0`
   - `OSAL_CORTEXM_HAS_DWT_CYCCNT != 0`
3. 向 `DEMCR` 置位 `TRCENA`
4. 如果平台声明需要 `LAR`，先向 `DWT LAR` 写解锁值
5. 把 `CYCCNT` 清零
6. 向 `DWT CTRL` 置位 `CYCCNTENA`
7. 读回 `DWT CTRL`，确认硬件是否真的接受了这次使能
8. 记录 `supported` 状态，清空 profiling 统计
9. 恢复中断

也就是说，当前代码并不是“只要宏开了就一定能测”，而是：

- 先看构建配置是否允许
- 再看当前内核是否真的支持
- 最后看硬件是否真的成功把 `CYCCNT` 打开

### 4.5 现在统计的是什么

当前 DWT profiling 只统计 `system` 层内部显式包裹的临界区，不统计整个系统所有关中断窗口。

主要覆盖：

- `mem`
- `queue`
- `timer`

不覆盖：

- 外部应用代码直接调 `osal_irq_disable()` 的时间
- `components` 层
- `platform/example` 层
- HAL 或板级驱动自己的临界区

### 4.6 统计值是怎么来的

内部状态主要记录：

- 是否支持
- 是否正在统计一笔最外层样本
- 嵌套深度
- 开始 cycle
- 样本数
- 最近一次、最小、最大、总 cycle

进入最外层 `system` 临界区时：

1. 读一次 `CYCCNT`
2. 记为 `start_cycles`
3. `depth++`

退出最外层 `system` 临界区时：

1. 再读一次 `CYCCNT`
2. 做差得到 `elapsed_cycles`
3. 更新 `last/min/max/total/sample_count`

因为只在最外层结算，所以嵌套临界区不会被重复累计多次。

## 5. MPU

当前 `cortexm` 里只预留了 `MPU` 分区注释，没有实际配置任何 MPU 寄存器。

也就是说：

- 当前版本没有碰 `MPU CTRL`
- 没有配置 region
- 没有做内存保护策略

这个区块现在纯粹是为了以后扩展时有一个明确落点。

## 6. 这层不负责什么

`cortexm` 的职责边界很明确：

- 它负责 Cortex-M 内核外设配置
- 它不负责 LED、USART、Flash 这类板级设备桥接
- 它不负责应用示例逻辑
- 它也不负责对外暴露通用 IRQ API，那部分属于 `irq`

如果你在移植一个新板子，通常先改的是：

- `osal_config.h`
- `osal_cortexm.h`

而不是直接把板级细节塞进 `cortexm.c`。
