#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "osal.h"

/*
 * 说明：
 * 1. 这个文件是“OSAL 功能使用示例集”，默认不建议直接加入正式工程编译。
 * 2. 你可以按功能把需要的示例片段复制到 main.c 或自己的 app 文件里。
 * 3. 每个小节都尽量把“创建 -> 启动/绑定 -> 使用”写完整。
 * 4. 如果某个功能天然依赖任务或回调，本节会把最小必需的任务函数或回调函数也一起给出。
 * 5. 当前工程里 osal.h 已经聚合了平台头和组件头，示例文件也只需要包含 osal.h。
 * 6. 复制到自己工程时，通常只需要保留你想用的那一节。
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
/* 函数说明：演示最基础任务调度流程的示例任务。 */
static void osal_example_basic_task(void *arg) {
    (void)arg;
    osal_platform_led1_toggle();
    (void)osal_task_sleep(NULL, 500U);
}

/* 函数说明：初始化基础任务调度示例。 */
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

/* 函数说明：演示互斥量保护临界区的示例任务。 */
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

/* 函数说明：初始化互斥量使用示例。 */
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

/* 函数说明：演示等待事件触发的示例任务。 */
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

/* 函数说明：演示周期性置位事件的示例任务。 */
static void osal_example_event_set_task(void *arg) {
    (void)arg;

    if (s_example_event == NULL) {
        return;
    }

    (void)osal_event_set(s_example_event);
    (void)osal_task_sleep(NULL, 1000U);
}

/* 函数说明：初始化事件对象使用示例。 */
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
 * 1. 创建一个“结构体消息队列”
 * 2. 高优先级发送任务投递消息
 * 3. 高优先级接收任务读取消息
 */
typedef struct {
    uint32_t sequence;
    uint8_t payload[8];
} osal_example_queue_message_t;

/* 队列示例补充说明：
 * 推荐先看下面的“队列初始化示例一/二”，再按需复制发送和接收任务。 */
static osal_queue_t *s_example_queue = NULL;
static osal_example_queue_message_t s_example_queue_storage[8];
static uint32_t s_example_queue_sequence = 0U;

static void osal_example_queue_producer_task(void *arg);
static void osal_example_queue_consumer_task(void *arg);

/* 队列示例说明：
 * 1. 队列项可以是结构体、指针、固定长度数组，或它们的组合。
 * 2. 下面同时给出“OSAL 堆创建”和“用户静态缓冲区创建”两种示例。
 * 3. 二选一使用即可，不建议在同一个工程里同时调用这两个初始化函数。
 */

/* 函数说明：启动队列发送任务和接收任务。 */
static void osal_example_queue_start_tasks(void) {
    osal_task_t *producer_task;
    osal_task_t *consumer_task;

    producer_task = osal_task_create(osal_example_queue_producer_task, NULL, OSAL_TASK_PRIORITY_HIGH);
    consumer_task = osal_task_create(osal_example_queue_consumer_task, NULL, OSAL_TASK_PRIORITY_HIGH);
    if (producer_task != NULL) {
        (void)osal_task_start(producer_task);
    }
    if (consumer_task != NULL) {
        (void)osal_task_start(consumer_task);
    }
}

/* 函数说明：演示向消息队列投递消息的发送任务。
 * 队列满时使用 OSAL_WAIT_FOREVER 真正阻塞等待空位。 */

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

    if (osal_queue_send_timeout(s_example_queue, &message, OSAL_WAIT_FOREVER) == OSAL_OK) {
        printf("queue send: seq=%lu first=%u count=%lu\r\n",
               (unsigned long)message.sequence,
               (unsigned int)message.payload[0],
               (unsigned long)osal_queue_get_count(s_example_queue));
    }

    (void)osal_task_sleep_until(NULL, 1000U);
}

/* 函数说明：演示从消息队列读取消息的接收任务。
 * send / send_from_isr 成功后，等待接收的任务会被直接置为 READY。 */
static void osal_example_queue_consumer_task(void *arg) {
    osal_example_queue_message_t message;

    (void)arg;
    if (s_example_queue == NULL) {
        return;
    }

    if (osal_queue_recv_timeout(s_example_queue, &message, OSAL_WAIT_FOREVER) == OSAL_OK) {
        printf("queue recv: seq=%lu bytes=%u,%u count=%lu\r\n",
               (unsigned long)message.sequence,
               (unsigned int)message.payload[0],
               (unsigned int)message.payload[1],
               (unsigned long)osal_queue_get_count(s_example_queue));
    }
}

/* 函数说明：初始化消息队列使用示例。
 * 下面同时给出“OSAL 堆创建”和“用户静态缓冲区创建”两种方式。
 * 二选一使用即可。 */
/* 队列初始化示例一：
 * 1. 使用 osal_queue_create() 从 OSAL 统一内存池创建队列。
 * 2. 队列控制块和消息缓冲区都由 OSAL 管理。
 * 3. 这版更适合直接照搬到 main/app 层。
 */
void osal_example_queue_demo_init(void) {
    s_example_queue = osal_queue_create(8U, (uint32_t)sizeof(osal_example_queue_message_t));
    if (s_example_queue == NULL) {
        return;
    }

    osal_example_queue_start_tasks();
}

/* 队列初始化示例二：
 * 1. 使用 osal_queue_create_static()，消息缓冲区由用户自己提供。
 * 2. 队列控制块仍然来自 OSAL 内存池。
 * 3. 如果你想手工控制消息缓冲区位置，就参考这一版。
 */
void osal_example_queue_demo_static_init(void) {
    s_example_queue = osal_queue_create_static(
        s_example_queue_storage,
        8U,
        (uint32_t)sizeof(osal_example_queue_message_t));
    if (s_example_queue == NULL) {
        return;
    }

    osal_example_queue_start_tasks();
}

/* ========================= 5. 软件定时器示例 =========================
 * 演示流程：
 * 1. 创建单次定时器
 * 2. 创建周期定时器
 * 3. 启动后由 osal_run() 在主循环里驱动调度
 */
/* 函数说明：演示单次软件定时器回调的示例函数。 */
static void osal_example_oneshot_timer_callback(void *arg) {
    (void)arg;
    printf("oneshot timer: %lu ms\r\n", (unsigned long)osal_timer_get_tick());
}

/* 函数说明：演示周期性软件定时器回调的示例函数。 */
static void osal_example_periodic_timer_callback(void *arg) {
    static uint32_t count = 0U;

    (void)arg;
    ++count;
    printf("periodic timer #%lu: %lu ms\r\n",
           (unsigned long)count,
           (unsigned long)osal_timer_get_tick());
}

/* 函数说明：初始化软件定时器使用示例。 */
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
 * 3. 之后 printf() 和 periph_uart_write_xxx() 都能直接使用
 */
static periph_uart_t *s_example_uart = NULL;

/* 函数说明：初始化 USART 控制台和输出示例。 */
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
/* 函数说明：执行一次内部 Flash 组件使用示例。 */
void osal_example_flash_demo_once(void) {
    periph_flash_t *flash;
    uint8_t payload[] = {0x52U, 0x56U, 0x4FU, 0x53U, 0x01U, 0x02U, 0x03U, 0x04U};
    uint32_t word0;
    uint32_t word1;
    uint8_t readback[sizeof(payload)];

    flash = osal_platform_flash_create();
    if (flash == NULL) {
        return;
    }

    memcpy(&word0, &payload[0], sizeof(word0));
    memcpy(&word1, &payload[4], sizeof(word1));
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
    /* 根据当前芯片支持的位宽，明确选择 write_u8/u16/u32/u64 中的一个或多个接口。 */
    if (periph_flash_write_u32(flash, OSAL_PLATFORM_FLASH_DEMO_ADDRESS, word0) != OSAL_OK) {
        printf("flash write word0 failed\r\n");
        (void)periph_flash_lock(flash);
        return;
    }
    if (periph_flash_write_u32(flash, OSAL_PLATFORM_FLASH_DEMO_ADDRESS + 4U, word1) != OSAL_OK) {
        printf("flash write word1 failed\r\n");
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
}
