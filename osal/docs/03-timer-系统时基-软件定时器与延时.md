# timer：系统时基、软件定时器与延时

## 1. 模块职责

`timer` 负责三类能力：

- 统一运行时间基准
- 忙等延时
- 软件定时器

其中前两类始终可用，软件定时器由 `OSAL_CFG_ENABLE_SW_TIMER` 控制。

## 2. 对外接口

基础时基接口：

- `osal_timer_get_uptime_us()`
- `osal_timer_get_uptime_ms()`
- `osal_timer_get_tick()`
- `osal_timer_delay_us()`
- `osal_timer_delay_ms()`
- `osal_timer_poll()`

软件定时器接口：

- `osal_timer_create()`
- `osal_timer_start()`
- `osal_timer_stop()`
- `osal_timer_delete()`

## 3. 时基来源

`timer` 并不直接依赖某一家 HAL，而是通过 `cortexm` 提供的 `osal_cortexm_tick_source_t` 读取原始 tick 源。

当前 `osal_cortexm_tick_source_t` 需要这些能力：

- 获取计数器输入时钟
- 获取重装值
- 获取当前计数值
- 判断计数器是否使能
- 判断当前周期是否已经发生回卷

在当前 STM32F4 工程里，这个原始 tick 源就是 `SysTick`。

## 4. 时间累计方式

模块内部同时维护：

- 32 位微秒累计
- 32 位毫秒累计
- 64 位微秒累计
- 毫秒换算余数

这样做的目的：

- 32 位计数方便与常见 MCU tick 用法保持一致
- 64 位计数给软件定时器和长时间运行场景使用
- 余数累计避免长期 `us -> ms` 换算误差

## 5. 子节拍读取

当前实现不仅累计“整 tick 时间”，还会在读取当前时间时补上“当前 tick 内已经过去的子节拍偏移”。

核心原因是：

- `SysTick` 是递减计数器
- 读取过程中可能恰好跨过回卷点

因此当前实现采用：

1. 读当前值
2. 读回卷标志
3. 再读一次当前值

再综合判断当前子节拍偏移。

## 6. 忙等延时

### 6.1 `delay_us()` / `delay_ms()`

这两个接口是同步忙等：

- 会占用 CPU
- 不会让出执行权
- 不适合作为协作任务里的长期周期控制手段

它们更适合：

- 极短时间硬件等待
- 初始化过程里的简单节拍延时

## 7. 软件定时器

### 7.1 数据结构

软件定时器内部维护这些关键信息：

- `active`
- `periodic`
- `expiry_us`
- `period_us`
- `cb`
- `arg`

### 7.2 调度方式

软件定时器不在 `SysTick` 中断里直接执行回调。

当前模型是：

1. `SysTick` 中断只负责累计时间
2. `osal_timer_poll()` 在任务态检查是否到期
3. 到期后在任务态执行回调

这样可以把中断里的工作量控制到最小。

### 7.3 最近到期时间优化

当前实现会缓存“当前所有活动定时器里最早到期的那个时间”。

效果是：

- 没到最近到期点之前，不需要每次都全表扫描
- 软件定时器数量不大时，这个模型足够直接且易维护

## 8. 32 位 tick 防溢出比较

### 8.1 为什么不能直接写 `now >= deadline`

因为 `now` 和 `deadline` 都可能在 32 位自然回绕后跨过 `0`。

例如：

- `deadline = 0xFFFFFFF0`
- 过了几十个 tick 后
- `now = 0x00000010`

这时简单的 `now >= deadline` 会得到错误结论。

### 8.2 推荐写法

对“有限时间窗口内的 deadline 判断”，推荐使用差值比较：

```c
static bool tick_reached(uint32_t now, uint32_t deadline) {
    return ((int32_t)(now - deadline) >= 0);
}
```

这个写法的意义是：

- 先让无符号减法自然回绕
- 再用有符号视角判断“now 是否已经越过 deadline”

### 8.3 当前 OSAL 里怎么用

当前仓库里主要有两类相关写法：

- 周期任务示例用 `((int32_t)(now_ms - deadline_ms) >= 0)` 判断是否到期
- `queue(timeout_ms)` 用无符号差值 `now - start` 判断是否超过等待窗口

两者本质上都是利用回绕安全的差值比较，而不是直接比较绝对值大小。

## 9. 使用边界

- `delay_us()` / `delay_ms()` 是忙等，不是任务调度原语
- 软件定时器回调运行在任务态，不在中断里执行
- `osal_timer_poll()` 需要被主循环或任务层持续推进
- 如果平台没有提供有效 tick 源，系统会退回到 `OSAL_TICK_PERIOD_US` 的保底配置
