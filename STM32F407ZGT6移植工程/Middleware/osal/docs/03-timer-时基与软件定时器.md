# timer：时基、溢出处理与软件定时器

## 1. 模块职责

`osal_timer` 负责四件事：

- 统一时基
- `tick / uptime` 查询
- 忙等延时
- 软件定时器

对应文件：

- [osal_timer.h](/abs/path/A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal/system/Inc/osal_timer.h)
- [osal_timer.c](/abs/path/A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal/system/Src/osal_timer.c)

## 2. 时间基准从哪里来

当前 OSAL 默认使用 `SysTick` 作为系统时基。

初始化时：

1. `osal_init()` 调用 platform 层初始化
2. system 层自动配置 `SysTick`
3. `timer` 从 `osal_platform_get_tick_source()` 拿到原始计数源

之后：

- `SysTick_Handler()` 里调用 `osal_tick_handler()`
- `osal_tick_handler()` 最终进入 `osal_timer_accumulate_us()`

所以时间累加是“中断里累加粗粒度时间”，不是每次查询都从零推导。

## 3. 为什么既有累计时间，又要读当前计数器

原因是要同时兼顾：

- 稳定的长期累计
- 更细的子 tick 精度

当前实现通常分两部分：

1. 累计的整 tick 时间
2. 当前 tick 内剩余计数值换算出来的子时间

这样 `get_uptime_us()` 才不会只有粗糙的 tick 分辨率。

## 4. 为什么 `get_uptime_us()` 要关中断

它不是为了“偷懒”，而是为了避免撕裂读取。

因为它同时要读：

- 已累计的粗粒度时间
- 当前 `SysTick` 计数器值
- `COUNTFLAG`

如果中间被 `SysTick` 中断打断，就可能出现：

- 粗粒度时间已经变了
- 子 tick 还是旧的

这会造成一次读取内部前后不一致。

所以要短时间关中断，把这一组读操作收成一个原子窗口。

## 5. 如何避免计时溢出问题

### 5.1 对外 32 位时间允许回绕

对外接口像：

- `osal_timer_get_tick()`
- `osal_timer_get_uptime_us()`

返回 32 位值，本来就会回绕。

OSAL 没试图禁止这个回绕，而是采用：

- 累计时间内部更高精度保存
- 比较超时时用差值比较

### 5.2 软件定时器内部用更宽位数

软件定时器到期时间通常用 `uint64_t expiry_us` 保存。

这样做的好处是：

- 软件定时器本身更不容易因为长期运行频繁回绕
- 周期定时器长期运行更稳

### 5.3 超时比较用差值

无论是任务超时还是定时器触发，核心思路都不是比“谁绝对更大”，而是比：

- `now - start`
- `now - deadline`

这类差值比较对回绕更稳。

## 6. `delay_us()` 和 `delay_ms()` 的本质

当前它们是忙等。

也就是说：

- 当前线程会一直占着 CPU
- 直到达到目标时间才返回

这和 `osal_task_sleep()` 完全不是一回事。

所以：

- 短延时可以用
- 长延时不建议用
- 任务场景优先用 `sleep / sleep_until`

## 7. 软件定时器怎么实现

### 7.1 基本模型

每个软件定时器条目通常包含：

- 是否激活
- 是否周期
- 到期时间
- 周期
- 回调函数
- 回调参数

### 7.2 创建与启动

创建阶段只是拿到一个条目并填写参数。

启动阶段才真正设置：

- `active = true`
- `expiry_us = now + period`

### 7.3 单次定时器

到期后：

- 执行回调
- 标记为不再活动

### 7.4 周期定时器

到期后：

- 执行回调
- 不是简单地“从当前时刻再加 period”
- 而是按已有到期基准继续向后累加

这能减少周期漂移。

## 8. 为什么软件定时器不需要每个 tick 都全表扫描

这是当前实现里最关键的性能点。

OSAL 会维护一个“最近到期时间”缓存：

- `s_next_expiry_valid`
- `s_next_expiry_us`

工作方式：

1. `osal_timer_poll()` 先取 `now_us`
2. 如果 `now_us` 还没到最近到期点
3. 直接返回

只有当 `now_us >= s_next_expiry_us` 时，才去扫描活动定时器表。

这带来的结果是：

- 空闲时开销很小
- 不需要每轮白扫所有 timer

## 9. 为什么软件定时器回调稳定

稳定的关键不是“每次中断里触发”，而是：

- 到期时间用绝对基准维护
- 最近到期时间做快返回
- 周期定时器到期后按周期锚点向后追赶

所以它通常比“任务里 sleep(500)”的周期更稳。

## 10. 当前 timer 模块的边界

当前设计的边界如下：

- 不是硬实时 timer
- 回调执行在 `osal_run()` 驱动的上下文，不是中断上下文
- `delay_us/delay_ms` 是忙等
- 如果主循环长期不执行 `osal_run()`，软件定时器回调也会被延后

## 11. 读源码时建议重点看

建议重点看：

- tick source 同步
- 子 tick 读取
- `get_uptime_us64()`
- `osal_timer_poll()`
- 周期 timer 到期后的“追赶”逻辑

这些地方决定了：

- 时间准不准
- 会不会因为回绕出问题
- 软件定时器会不会空跑扫描
