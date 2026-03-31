# OSAL 使用示例

当前参考示例文件位于：

- `Middleware/osal/platform/example/stm32f4/osal_integration_stm32f4.c`

## 1. 任务创建与启动

最小示例：

```c
static void app_task(void *arg)
{
    (void)arg;
    osal_platform_led1_toggle();
    (void)osal_task_sleep(NULL, 500U);
}

void app_task_demo_init(void)
{
    osal_task_t *task = osal_task_create(app_task, NULL, OSAL_TASK_PRIORITY_LOW);
    if (task != NULL) {
        (void)osal_task_start(task);
    }
}
```

## 2. 事件

事件示例在 `osal_integration_stm32f4.c` 里分成了：

- 等待任务
- 置位任务
- 初始化函数

适合复制到应用层做最小联调。

## 3. 互斥量

互斥量示例里演示了：

- 创建互斥量
- 两个任务竞争同一把锁
- 修改共享计数器

## 4. 队列

队列示例使用结构体消息：

```c
typedef struct {
    uint32_t sequence;
    uint8_t payload[8];
} osal_example_queue_message_t;
```

它演示了：

- 静态消息缓存区创建
- 高优先级生产者
- 高优先级消费者

## 5. 软件定时器

示例中同时演示了：

- 单次软件定时器
- 周期性软件定时器

## 6. USART 组件

示例里演示了：

- 创建平台串口组件
- 绑定控制台
- 发送字符串
- 发送数组

## 7. Flash 组件

示例里演示了：

- 解锁
- 擦除
- 写入
- 回读
- 上锁

## 8. 适配层文件职责

- `platform/osal_platform_cortexm.c/.h`
  通用模板，只负责告诉用户哪些接口需要填写。

- `platform/example/stm32f4/osal_platform_stm32f4.c/.h`
  根据模板填写出的 STM32F4 适配层。

- `platform/example/stm32f4/osal_integration_stm32f4.c`
  组件使用示例集。
