# rivers_osal

`rivers_osal` 是一套面向 32 位 MCU 裸机场景的轻量 OSAL。

仓库根目录下的 [`osal/`](osal/) 就是可直接复用的主体内容，包含：

- `system/`：任务、队列、事件、互斥量、定时器、内存管理、中断抽象
- `components/`：外设桥接等可复用小组件
- `examples/`：平台适配与集成示例

建议先阅读：

- `osal/README.md`
- `osal/PORTING_GUIDE.md`
- `osal/USAGE_EXAMPLES.md`
