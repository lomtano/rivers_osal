# components：外围组件与示例

## 1. components 的定位

`components` 不是内核。

它们的作用是：

- 在 OSAL 之上挂一些常用能力
- 用桥接模式降低对具体 MCU SDK 的耦合

当前主要包括：

- `periph/usart`
- `periph/flash`
- `RTT`
- `KEY`

## 2. usart 组件

### 设计目标

`USART` 组件的核心思想很简单：

- 上层不直接依赖 HAL/StdPeriph/GD32/N32 SDK
- 上层只依赖“发送一个字节”这个最小能力

### 桥接方式

桥接表只要提供单字节发送函数：

- `write_byte`

之后组件层就能封装出：

- 写一个字节
- 写字符串
- 写缓冲区
- `fputc` / `printf` 控制台重定向

### 优点

- 接口很薄
- 容易跨平台
- 上层逻辑不关心底层具体 SDK 名字

### 边界

当前是阻塞逐字节发送，不是 DMA，也没有接收路径。

## 3. flash 组件

### 设计目标

Flash 组件负责：

- 解锁
- 上锁
- 擦除
- 读取
- 显式位宽写入

### 现在为什么只保留显式位宽接口

当前只保留：

- `write_u8`
- `write_u16`
- `write_u32`
- `write_u64`

原因是：

- 不同芯片支持的可写位宽不一样
- 让组件自动猜测位宽，容易在某些芯片上失败
- 显式接口更直观，也更符合裸机开发习惯

### 当前结论

这套设计把“用什么位宽写”这个决策交回给用户和平台桥接，不再由组件替用户决定。

## 4. RTT 组件

### 角色

当前 RTT 相关文件本质上是第三方组件。

用途主要是：

- 调试输出
- 日志查看

### 建议

这类第三方代码一般不建议按 OSAL 风格大幅改注释，因为：

- 后续升级 diff 会很脏
- 维护成本高

## 5. KEY 组件

### 角色

`KEY` 目录下的 `multi_button` 本质上也是偏独立的按钮状态机组件。

### 建议

同样不建议按 OSAL 内核风格去大改实现注释。

更合适的做法是只补一段说明：

- 这是第三方/独立状态机组件
- 需要固定周期调用 `button_ticks()`

## 6. integration 示例文件

当前示例文件是：

- [osal_integration_stm32f4.c](/abs/path/A:/Embedded_system/cubemx_project/rivers_osal/Middleware/osal/platform/example/stm32f4/osal_integration_stm32f4.c)

这个文件的职责不是板级适配，而是“怎么用 OSAL”。

它适合放：

- 任务示例
- 队列示例
- 软件定时器示例
- USART 示例
- Flash 示例

它不适合承载：

- 内核实现细节
- MCU 平台底层初始化逻辑

## 7. main.c 和 integration 的关系

当前推荐关系是：

- `main.c`
  - 作为当前板子真实运行入口
  - 保留最小可运行示例
- `integration`
  - 作为“功能示例合集”
  - 方便复制某一段功能到应用层

所以当你新增一个系统功能的示例时，最好同步更新：

- `main.c` 的运行版示例
- `osal_integration_stm32f4.c` 的说明型示例

## 8. 组件层的整体原则

当前组件层最值得坚持的原则是：

- 组件只解决“能力封装”
- 不侵入 system 核心
- 不把厂商 SDK 直接拉进内核
- 能桥接就桥接，能显式就显式

这也是这套系统后续继续扩展 `bootloader / rtt / 其他外设组件` 时最重要的约束。
