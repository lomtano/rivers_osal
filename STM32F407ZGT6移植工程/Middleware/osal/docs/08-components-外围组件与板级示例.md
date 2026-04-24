# components：外围组件与板级示例

这一篇不再解释 OSAL 内核本体，而是专门说明 `components` 和 `platform/example` 这两层的职责。

最简单的理解方式是：

- `system`：OSAL 内核
- `cortexm`：OSAL 依赖的 Cortex-M 内核外设配置
- `components`：构建在 OSAL 之上的可复用外围组件
- `platform/example`：针对具体板子的桥接和演示接线方式

## 1. `components` 的定位

`components` 不是调度器，也不是内核同步原语。

它的角色更接近“建立在 OSAL 之上的上层模块”，例如：

- `USART`
- `Flash`
- `RTT`
- `KEY`

这层的目标，是把常用外设能力组织成更容易复用的接口，但不把板级细节硬塞回 `system`。

## 2. USART 组件

`USART` 组件的定位是：

- 利用 OSAL 的队列、任务或时基能力组织串口收发
- 对上层暴露相对稳定的接口
- 把真正的 UART 外设访问留在板级桥接里

这样拆分的好处是：

- `system` 不需要知道具体是哪路 UART
- `components/periph/usart` 可以在不同工程里复用
- 不同板子的 GPIO、DMA、IRQ 绑定可以放进各自的桥接文件

## 3. Flash 组件

`Flash` 组件的定位是：

- 给应用层提供更直接的数据读写入口
- 统一显式位宽接口和最小必要的读写语义
- 把底层芯片相关的擦写细节隔离在桥接实现里

当前它更适合做：

- 参数区读写
- 小块配置保存
- 示例级别的 Flash 访问验证

而不是把整个文件系统或复杂磨损均衡逻辑塞进 OSAL。

## 4. RTT 组件

`RTT` 组件主要承担调试输出通道的角色。

它适合：

- 调试日志
- 不占用串口引脚的打印
- 和串口 demo 并行存在的辅助输出

需要注意的是，RTT 输出看的是 RTT viewer / RTT client，不是普通串口终端。

## 5. KEY 组件

`KEY` 组件当前主要服务按键输入这类外围能力。

它适合：

- 简单按键事件
- 去抖后的状态输入
- 示例工程里的最小交互入口

如果后面要扩展更复杂的输入模型，也应该继续留在 `components` 层，而不是回灌到 `system`。

## 6. `platform/example` 的定位

`platform/example/stm32f4` 这层不是内核，也不是通用组件。

它的职责是：

- 把当前 STM32F407 示例板的 LED、USART、Flash 等硬件接到 OSAL 或 components
- 提供一份可运行、可复制的板级集成示例
- 告诉使用者“新板子大概应该在哪里接线”

因此它更像“板级桥接样例”，而不是一套必须原封不动复用的公共库。

## 7. `main.c` 和 `osal_integration_stm32f4.c` 的关系

当前工程里有两类示例入口：

### 7.1 `Core/Src/main.c`

它展示的是：

- OSAL 初始化顺序
- 任务、队列、软件定时器、RTT、DWT profiling、Flash demo 的最小用法
- 一个完整工程里这些能力如何串起来

适合第一次看整体运行方式的人。

### 7.2 `platform/example/stm32f4/osal_integration_stm32f4.c`

它展示的是：

- 如果不依赖当前 `main.c` 组织方式，怎样做一份更独立的板级集成示例
- 哪些 helper/bridge 更适合放在板级示例文件里

适合要移植到新板子的人参考。

## 8. 使用边界

- `components` 不负责 OSAL 内核调度语义
- `platform/example` 不负责 `SysTick / NVIC / DWT` 这类 Cortex-M 内核外设配置
- `system` 不负责板级 LED/UART/Flash 细节
- 如果某段代码明显依赖具体板子，它就不该放回 `system`

## 9. 推荐使用方式

- 想看 OSAL 核心怎么工作，先读 `01` 到 `07`
- 想看串口、Flash、RTT、KEY 这些外围能力，再读这一篇
- 想做新板移植时，优先参考 `platform/example` 的桥接思路，而不是直接修改 `system`
