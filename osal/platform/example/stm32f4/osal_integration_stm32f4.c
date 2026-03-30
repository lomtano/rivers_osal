#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "osal.h"
#include "periph_uart.h"
#include "periph_flash.h"
#include "osal_platform_stm32f4.h"

/*
 * 说明：
 * 1. 这个文件是“OSAL 功能使用示例集”，默认不建议直接加入正式工程编译。
 * 2. 你可以按功能把需要的示例片段复制到 main.c 或自己的 app 文件里。
 * 3. 每个小节都尽量把“创建 -> 启动/绑定 -> 使用”写完整。
 * 4. 如果某个功能天然依赖任务或回调，本节会把最小必需的任务函数 / 回调函数也一起给出。
 * 5. 复制到自己工程时，通常只需要保留你想用的那一节。
 */

/* ========================= 0. main 最小接入顺序示例 =========================
 * 下面这段通常放在 main() 的硬件初始化之后：
 *
 *     osal_init();
 *     osal_example_usart_demo_init();
 *     osal_example_task_demo_init();
 *     osal_example_timer_demo_init();
 *
 *     while (1) {
 *         osal_run();
 *     }
 *
 * 同时要记得在系统时基中断里调用：
 *     osal_tick_handler();
 */

/* ========================= 1. 创建任务并启动任务 =========================
 * 复制本节时，请把下面两个部分一起复制：
 * 1. 任务入口函数
 * 2. 对应的 demo_init() 初始化函数
 */
static void osal_example_basic_task(void *arg) {
    (void)arg;
    osal_platform_led1_toggle();
    (void)osal_task_sleep(NULL, 500U);
}

void osal_example_task_demo_init(void) {
    osal_task_t *task = osal_task_create(osal_example_basic_task, NULL, OSAL_TASK_PRIORITY_LOW);

    if (task != NULL) {
        (void)osal_task_start(task);
    }
}

/* ========================= 2. 互斥量示例 =========================
 * 演示流程：
 * 1. 创建互斥量
 * 2. 创建两个访问共享资源的任务
 * 3. 在任务里 lock / unlock
 */
static osal_mutex_t *s_example_mutex = NULL;
static uint32_t s_example_shared_counter = 0U;

static void osal_example_mutex_task(void *arg) {
    const char *name = (const char *)arg;

    if ((name == NULL) || (s_example_mutex == NULL)) {
        return;
    }

    if (osal_mutex_lock(s_example_mutex, 10U) == OSAL_OK) {
        ++s_example_shared_counter;
        printf("%s lock ok, counter=%lu\r\n", name, (unsigned long)s_example_shared_counter);
        (void)osal_mutex_unlock(s_example_mutex);
    }

    (void)osal_task_sleep(NULL, 200U);
}

void osal_example_mutex_demo_init(void) {
    osal_task_t *task_a;
    osal_task_t *task_b;

    s_example_mutex = osal_mutex_create();
    if (s_example_mutex == NULL) {
        return;
    }

    task_a = osal_task_create(osal_example_mutex_task, "mutex_task_a", OSAL_TASK_PRIORITY_MEDIUM);
    task_b = osal_task_create(osal_example_mutex_task, "mutex_task_b", OSAL_TASK_PRIORITY_MEDIUM);
    if (task_a != NULL) {
        (void)osal_task_start(task_a);
    }
    if (task_b != NULL) {
        (void)osal_task_start(task_b);
    }
}

/* ========================= 3. 事件示例 =========================
 * 演示流程：
 * 1. 创建事件对象
 * 2. 一个任务等待事件
 * 3. 另一个任务周期性置位事件
 */
static osal_event_t *s_example_event = NULL;

static void osal_example_event_wait_task(void *arg) {
    (void)arg;

    if (s_example_event == NULL) {
        return;
    }

    if (osal_event_wait(s_example_event, 1000U) == OSAL_OK) {
        printf("event wait ok\r\n");
    } else {
        printf("event wait timeout\r\n");
    }

    (void)osal_task_sleep(NULL, 100U);
}

static void osal_example_event_set_task(void *arg) {
    (void)arg;

    if (s_example_event == NULL) {
        return;
    }

    (void)osal_event_set(s_example_event);
    (void)osal_task_sleep(NULL, 1000U);
}

void osal_example_event_demo_init(void) {
    osal_task_t *wait_task;
    osal_task_t *set_task;

    s_example_event = osal_event_create(true);
    if (s_example_event == NULL) {
        return;
    }

    wait_task = osal_task_create(osal_example_event_wait_task, NULL, OSAL_TASK_PRIORITY_MEDIUM);
    set_task = osal_task_create(osal_example_event_set_task, NULL, OSAL_TASK_PRIORITY_MEDIUM);
    if (wait_task != NULL) {
        (void)osal_task_start(wait_task);
    }
    if (set_task != NULL) {
        (void)osal_task_start(set_task);
    }
}

/* ========================= 4. 队列示例 =========================
 * 演示流程：
 * 1. 静态创建一个“结构体消息队列”
 * 2. 高优先级生产者任务发消息
 * 3. 高优先级消费者任务收消息
 */
typedef struct {
    uint32_t sequence;
    uint8_t payload[8];
} osal_example_queue_message_t;

static osal_queue_t *s_example_queue = NULL;
static osal_example_queue_message_t s_example_queue_storage[8];
static uint32_t s_example_queue_sequence = 0U;

static void osal_example_queue_producer_task(void *arg) {
    osal_example_queue_message_t message;
    uint32_t i;

    (void)arg;
    if (s_example_queue == NULL) {
        return;
    }

    message.sequence = s_example_queue_sequence++;
    for (i = 0U; i < (uint32_t)sizeof(message.payload); ++i) {
        message.payload[i] = (uint8_t)(message.sequence + i);
    }

    if (osal_queue_send(s_example_queue, &message) == OSAL_OK) {
        printf("queue send: seq=%lu first=%u count=%lu\r\n",
               (unsigned long)message.sequence,
               (unsigned int)message.payload[0],
               (unsigned long)osal_queue_get_count(s_example_queue));
    }

    (void)osal_task_sleep(NULL, 1000U);
}

static void osal_example_queue_consumer_task(void *arg) {
    osal_example_queue_message_t message;

    (void)arg;
    if (s_example_queue == NULL) {
        return;
    }

    if (osal_queue_recv(s_example_queue, &message) == OSAL_OK) {
        printf("queue recv: seq=%lu bytes=%u,%u count=%lu\r\n",
               (unsigned long)message.sequence,
               (unsigned int)message.payload[0],
               (unsigned int)message.payload[1],
               (unsigned long)osal_queue_get_count(s_example_queue));
        return;
    }

    (void)osal_task_sleep(NULL, 10U);
}

void osal_example_queue_demo_init(void) {
    osal_task_t *producer_task;
    osal_task_t *consumer_task;

    s_example_queue = osal_queue_create_static(
        s_example_queue_storage,
        8U,
        (uint32_t)sizeof(osal_example_queue_message_t));
    if (s_example_queue == NULL) {
        return;
    }

    producer_task = osal_task_create(osal_example_queue_producer_task, NULL, OSAL_TASK_PRIORITY_HIGH);
    consumer_task = osal_task_create(osal_example_queue_consumer_task, NULL, OSAL_TASK_PRIORITY_HIGH);
    if (producer_task != NULL) {
        (void)osal_task_start(producer_task);
    }
    if (consumer_task != NULL) {
        (void)osal_task_start(consumer_task);
    }
}

/* ========================= 5. 软件定时器示例 =========================
 * 演示流程：
 * 1. 创建单次定时器
 * 2. 创建周期定时器
 * 3. 启动后由 osal_run() 在主循环里驱动调度
 */
static void osal_example_oneshot_timer_callback(void *arg) {
    (void)arg;
    printf("oneshot timer: %lu ms\r\n", (unsigned long)osal_timer_get_tick());
}

static void osal_example_periodic_timer_callback(void *arg) {
    static uint32_t count = 0U;

    (void)arg;
    ++count;
    printf("periodic timer #%lu: %lu ms\r\n",
           (unsigned long)count,
           (unsigned long)osal_timer_get_tick());
}

void osal_example_timer_demo_init(void) {
    int oneshot_timer = osal_timer_create(2000000U, false, osal_example_oneshot_timer_callback, NULL);
    int periodic_timer = osal_timer_create(1000000U, true, osal_example_periodic_timer_callback, NULL);

    if (oneshot_timer >= 0) {
        (void)osal_timer_start(oneshot_timer);
    }
    if (periodic_timer >= 0) {
        (void)osal_timer_start(periodic_timer);
    }
}

/* ========================= 6. USART 组件示例 =========================
 * 复制本节时，通常你只需要：
 * 1. 调用 osal_platform_uart_create() 创建 USART 组件对象
 * 2. 调用 periph_uart_bind_console() 绑定控制台
 * 3. 之后 printf() 和 periph_uart_write_xxx() 都能直接用
 */
static periph_uart_t *s_example_uart = NULL;

void osal_example_usart_demo_init(void) {
    static const uint8_t raw_bytes[] = {'a', 'b', 'c', '\r', '\n'};

    s_example_uart = osal_platform_uart_create();
    if (s_example_uart == NULL) {
        return;
    }

    (void)periph_uart_bind_console(s_example_uart);
    printf("\r\nOSAL STM32F4 demo\r\n");
    printf("tick source: osal_init() + SysTick counter\r\n");
    (void)periph_uart_write_string(s_example_uart, "osal usart demo\r\n");
    (void)periph_uart_write(s_example_uart, raw_bytes, (uint32_t)sizeof(raw_bytes));
}

/* ========================= 7. Flash 组件示例 =========================
 * 复制本节时，这一整段已经包含了：
 * 1. 创建 Flash 组件对象
 * 2. 解锁
 * 3. 擦除
 * 4. 写入
 * 5. 回读
 * 6. 上锁
 */
void osal_example_flash_demo_once(void) {
    periph_flash_t *flash;
    uint8_t payload[] = {0x52U, 0x56U, 0x4FU, 0x53U, 0x01U, 0x02U, 0x03U, 0x04U};
    uint8_t readback[sizeof(payload)];

    flash = osal_platform_flash_create();
    if (flash == NULL) {
        return;
    }

    memset(readback, 0, sizeof(readback));
    if (periph_flash_unlock(flash) != OSAL_OK) {
        printf("flash unlock failed\r\n");
        return;
    }
    if (periph_flash_erase(flash, OSAL_PLATFORM_FLASH_DEMO_ADDRESS, sizeof(payload)) != OSAL_OK) {
        printf("flash erase failed\r\n");
        (void)periph_flash_lock(flash);
        return;
    }
    if (periph_flash_write(flash, OSAL_PLATFORM_FLASH_DEMO_ADDRESS, payload, sizeof(payload)) != OSAL_OK) {
        printf("flash write failed\r\n");
        (void)periph_flash_lock(flash);
        return;
    }
    if (periph_flash_read(flash, OSAL_PLATFORM_FLASH_DEMO_ADDRESS, readback, sizeof(readback)) != OSAL_OK) {
        printf("flash read failed\r\n");
        (void)periph_flash_lock(flash);
        return;
    }

    (void)periph_flash_lock(flash);
    printf("flash readback: %02X %02X %02X %02X\r\n",
           readback[0], readback[1], readback[2], readback[3]);
}
