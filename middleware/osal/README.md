# OSAL Middleware

这是一个面向裸机、可跨 32 位 MCU 移植的轻量级 OSAL 雏形。核心层不依赖具体厂商 SDK，平台相关代码建议放在 `examples/` 或你自己的板级适配目录里。

## 当前设计

- 核心控制对象统一从 `osal_mem` 管理的静态 OSAL 堆分配
- 任务、队列、事件、互斥量、软件定时器都对用户暴露为 opaque handle
- 外设桥接组件独立放在 `middleware/components/`
- 中断相关接口通过 `osal_irq_disable()`、`osal_irq_restore()`、`osal_irq_is_in_isr()` 由平台实现
- 软件定时器由 `osal_run()` 自动轮询，不需要你在主循环里额外调用 `osal_timer_poll()`

## 目录结构

- `Inc/`：OSAL 公共头文件
- `Src/`：OSAL 核心实现
- `examples/stm32f4/`：STM32F4 适配与示例

## 接入方式

1. 把 `middleware/osal/Inc` 加入 include path。
2. 把 `middleware/osal/Src` 下的源文件加入工程。
3. 把 `middleware/components/periph` 和 `middleware/components/flash` 一起加入工程。
4. 提供平台相关的中断接口：

```c
uint32_t osal_irq_disable(void);
void osal_irq_restore(uint32_t prev_state);
bool osal_irq_is_in_isr(void);
```

5. 选择一种时间基准接法：

- 方式 A：在 1us 周期定时器中断里调用 `osal_timer_inc_tick()`
- 方式 B：用 `osal_timer_set_us_provider()` 绑定一个真实的硬件 us 计数器

6. 主循环里持续调用：

```c
while (1) {
    osal_run();
}
```

## 平台桥接建议

- UART、Flash、SPI、I2C 这类外设抽象建议都放在 `middleware/components/`
- OSAL 核心不要直接依赖任何具体 MCU SDK
- STM32F4 示例只作为参考，移植到 GD32、N32 时只需要改平台桥接，不需要改 OSAL 核心

## 文档

- 移植步骤：`middleware/PORTING_GUIDE.md`
- 使用示例：`middleware/USAGE_EXAMPLES.md`
