#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "osal.h"

/*
 * 说明：
 * 1. 这个文件是“OSAL 功能使用示例集”，默认不建议直接加入正式工程编译。
 * 2. 你可以按功能把需要的示例片段复制到 main.c 或自己的 app 文件里。
 * 3. 当前版本只保留和“纯协作式 task + 同步 queue + timer + irq/platform”一致的示例。
 */

typedef struct {
    uint32_t interval_ms;
    uint32_t next_run_ms;
    bool initialized;
} osal_example_periodic_ctx_t;

typedef struct {
    osal_example_periodic_ctx_t cadence;
    void (*toggle)(void);
} osal_example_led_task_ctx_t;

typedef struct {
    uint32_t sequence;
    uint8_t payload[8];
} osal_example_queue_message_t;

typedef struct {
    osal_example_periodic_ctx_t cadence;
    uint32_t next_sequence;
} osal_example_queue_producer_ctx_t;

#if OSAL_CFG_ENABLE_FLASH && OSAL_PLATFORM_ENABLE_FLASH_DEMO
typedef struct {
    periph_flash_t *flash;
    bool done;
} osal_example_flash_demo_ctx_t;
#endif

static osal_queue_t *s_example_queue = NULL;
static osal_example_led_task_ctx_t s_example_led1_ctx = {
    {500U, 0U, false},
    osal_platform_led1_toggle
};
static osal_example_led_task_ctx_t s_example_led2_ctx = {
    {1000U, 0U, false},
    osal_platform_led2_toggle
};
static osal_example_queue_producer_ctx_t s_example_queue_producer_ctx = {
    {1000U, 0U, false},
    0U
};
static osal_example_periodic_ctx_t s_example_rtt_period = {500U, 0U, false};
static periph_uart_t *s_example_uart = NULL;

#if OSAL_CFG_ENABLE_FLASH && OSAL_PLATFORM_ENABLE_FLASH_DEMO
static periph_flash_t *s_example_flash = NULL;
static osal_example_flash_demo_ctx_t s_example_flash_demo_ctx = {0};
#endif

/* 用差值比较判断 deadline 是否到达，允许 32 位 tick 自然回绕。 */
/* 函数说明：用回绕安全的差值比较判断 deadline 是否已到。 */
static bool osal_example_tick_reached(uint32_t now_ms, uint32_t deadline_ms) {
    return ((int32_t)(now_ms - deadline_ms) >= 0);
}

/* 函数说明：判断某个简单周期任务在当前 tick 下是否应执行。 */
static bool osal_example_periodic_is_due(osal_example_periodic_ctx_t *ctx, uint32_t now_ms) {
    if ((ctx == NULL) || (ctx->interval_ms == 0U)) {
        return false;
    }

    if (!ctx->initialized) {
        ctx->next_run_ms = now_ms;
        ctx->initialized = true;
    }

    return osal_example_tick_reached(now_ms, ctx->next_run_ms);
}

/* 函数说明：把周期任务的下一次运行点向后推进到未来。 */
static void osal_example_periodic_mark_run(osal_example_periodic_ctx_t *ctx, uint32_t now_ms) {
    if ((ctx == NULL) || (ctx->interval_ms == 0U)) {
        return;
    }

    if (!ctx->initialized) {
        ctx->next_run_ms = now_ms;
        ctx->initialized = true;
    }

    do {
        /* 即便主循环某次晚了几拍，这里也按固定节拍补齐，避免长期漂移。 */
        ctx->next_run_ms += ctx->interval_ms;
    } while (osal_example_tick_reached(now_ms, ctx->next_run_ms));
}

/* ========================= 0. main 最小接入顺序示例 =========================
 * 下面这段通常放在 main() 的硬件初始化之后：
 *
 *     osal_init();
 *     osal_example_usart_demo_init();
 *     osal_example_task_demo_init();
 *     osal_example_queue_demo_init();
 *     osal_example_timer_demo_init();
 *
 *     while (1) {
 *         osal_run();
 *     }
 *
 * 同时要记得在系统时基中断里调用：
 *     osal_tick_handler();
 */

/* ========================= 1. 任务示例 =========================
 * 当前 task 模块只负责协作式调度。
 * 如果你想实现“500ms 执行一次”这种节拍，应该在任务层自己维护状态机，
 * 而不是依赖已经删除的旧阻塞周期接口。
 */
/* 函数说明：示例点灯任务，按各自节拍翻转对应 LED。 */
static void osal_example_led_task(void *arg) {
    osal_example_led_task_ctx_t *ctx = (osal_example_led_task_ctx_t *)arg;
    uint32_t now_ms;

    if ((ctx == NULL) || (ctx->toggle == NULL)) {
        return;
    }

    now_ms = osal_timer_get_tick();
    if (!osal_example_periodic_is_due(&ctx->cadence, now_ms)) {
        return;
    }

    ctx->toggle();
    osal_example_periodic_mark_run(&ctx->cadence, now_ms);
}

/* 函数说明：初始化两个低优先级 LED 示例任务。 */
void osal_example_task_demo_init(void) {
    osal_task_t *led1_task;
    osal_task_t *led2_task;

    led1_task = osal_task_create(osal_example_led_task, &s_example_led1_ctx, OSAL_TASK_PRIORITY_LOW);
    led2_task = osal_task_create(osal_example_led_task, &s_example_led2_ctx, OSAL_TASK_PRIORITY_LOW);
    if (led1_task != NULL) {
        (void)osal_task_start(led1_task);
    }
    if (led2_task != NULL) {
        (void)osal_task_start(led2_task);
    }
}

/* ========================= 2. 队列示例 =========================
 * 当前 queue 是同步接口：
 * 1. timeout_ms = 0U 表示立即尝试一次。
 * 2. timeout_ms = N 表示在 N ms 窗口内同步忙等重试。
 * 3. 它不会把当前任务挂起，也不会在内部调用 yield()。
 *
 * 因此，当队列状态依赖“另一个协作任务”推进时，任务里通常应该使用 timeout_ms = 0U，
 * 然后由任务层自己决定下一轮何时重试。
 */
/* 函数说明：示例队列生产者任务，周期性尝试发送一个消息。 */
static void osal_example_queue_producer_task(void *arg) {
    osal_example_queue_producer_ctx_t *ctx = (osal_example_queue_producer_ctx_t *)arg;
    osal_example_queue_message_t message;
    uint32_t now_ms;
    uint32_t i;
    osal_status_t status;

    if ((ctx == NULL) || (s_example_queue == NULL)) {
        return;
    }

    now_ms = osal_timer_get_tick();
    if (!osal_example_periodic_is_due(&ctx->cadence, now_ms)) {
        return;
    }

    message.sequence = ctx->next_sequence;
    for (i = 0U; i < (uint32_t)sizeof(message.payload); ++i) {
        /* 构造一个递增 payload，方便串口观察收发是否对齐。 */
        message.payload[i] = (uint8_t)(message.sequence + i);
    }

    status = osal_queue_send(s_example_queue, &message, 0U);
    if (status == OSAL_OK) {
        printf("queue send: seq=%lu first=%u count=%lu\r\n",
               (unsigned long)message.sequence,
               (unsigned int)message.payload[0],
               (unsigned long)osal_queue_get_count(s_example_queue));
        ++ctx->next_sequence;
        osal_example_periodic_mark_run(&ctx->cadence, now_ms);
    }
}

/* 函数说明：示例队列消费者任务，非阻塞读取一帧消息并打印。 */
static void osal_example_queue_consumer_task(void *arg) {
    osal_example_queue_message_t message;
    osal_status_t status;

    (void)arg;
    if (s_example_queue == NULL) {
        return;
    }

    status = osal_queue_recv(s_example_queue, &message, 0U);
    if (status == OSAL_OK) {
        printf("queue recv: seq=%lu bytes=%u,%u count=%lu\r\n",
               (unsigned long)message.sequence,
               (unsigned int)message.payload[0],
               (unsigned int)message.payload[1],
               (unsigned long)osal_queue_get_count(s_example_queue));
    }
}

/* 函数说明：初始化同步队列示例及其生产者/消费者任务。 */
void osal_example_queue_demo_init(void) {
    osal_task_t *producer_task;
    osal_task_t *consumer_task;

    s_example_queue = osal_queue_create(8U, (uint32_t)sizeof(osal_example_queue_message_t));
    if (s_example_queue == NULL) {
        return;
    }

    producer_task = osal_task_create(
        osal_example_queue_producer_task,
        &s_example_queue_producer_ctx,
        OSAL_TASK_PRIORITY_HIGH);
    consumer_task = osal_task_create(
        osal_example_queue_consumer_task,
        NULL,
        OSAL_TASK_PRIORITY_HIGH);
    if (producer_task != NULL) {
        (void)osal_task_start(producer_task);
    }
    if (consumer_task != NULL) {
        (void)osal_task_start(consumer_task);
    }
}

/* 这个辅助函数更适合 ISR / DMA 驱动场景：
 * 如果资源变化来自异步硬件路径，那么同步 timeout_ms 忙等才有实际意义。 */
/* 函数说明：给 ISR/DMA 驱动场景准备的同步队列发送辅助函数。 */
osal_status_t osal_example_queue_send_polling(
    osal_queue_t *queue,
    const osal_example_queue_message_t *message,
    uint32_t timeout_ms) {
    if ((queue == NULL) || (message == NULL)) {
        return OSAL_ERR_PARAM;
    }

    return osal_queue_send(queue, message, timeout_ms);
}

/* ========================= 3. 软件定时器示例 ========================= */
/* 函数说明：单次软件定时器示例回调。 */
static void osal_example_oneshot_timer_callback(void *arg) {
    (void)arg;
    printf("oneshot timer: %lu ms\r\n", (unsigned long)osal_timer_get_tick());
}

/* 函数说明：周期软件定时器示例回调。 */
static void osal_example_periodic_timer_callback(void *arg) {
    static uint32_t count = 0U;

    (void)arg;
    ++count;
    printf("periodic timer #%lu: %lu ms\r\n",
           (unsigned long)count,
           (unsigned long)osal_timer_get_tick());
}

/* 函数说明：创建并启动一个单次定时器和一个周期定时器示例。 */
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

/* ========================= 4. RTT / yield 观察示例 =========================
 * yield 的语义是“在当前调用栈里同步触发一次嵌套调度”，不是任务挂起。
 * 下面这个任务每 500ms 打印一次 tick，并在末尾主动让出一次执行机会。
 */
/* 函数说明：RTT 示例任务，周期打印 tick 并主动 yield 一次。 */
static void osal_example_rtt_task(void *arg) {
    static bool s_rtt_initialized = false;
    uint32_t now_ms;

    (void)arg;
    if (!s_rtt_initialized) {
        SEGGER_RTT_Init();
        s_rtt_initialized = true;
    }

    now_ms = osal_timer_get_tick();
    if (!osal_example_periodic_is_due(&s_example_rtt_period, now_ms)) {
        return;
    }

    printf("rtt task running: %lu ms\r\n", (unsigned long)now_ms);
    osal_example_periodic_mark_run(&s_example_rtt_period, now_ms);
    osal_task_yield();
}

/* 函数说明：初始化 RTT/yield 观察示例任务。 */
void osal_example_rtt_demo_init(void) {
    osal_task_t *task = osal_task_create(osal_example_rtt_task, NULL, OSAL_TASK_PRIORITY_MEDIUM);

    if (task != NULL) {
        (void)osal_task_start(task);
    }
}

/* ========================= 5. USART 组件示例 ========================= */
/* 函数说明：初始化串口桥接示例并打印几条启动信息。 */
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

#if OSAL_CFG_ENABLE_IRQ_PROFILE
/* ========================= 6. IRQ profiling 示例 ========================= */
static osal_example_periodic_ctx_t s_example_profile_period = {
    OSAL_CORTEXM_CRITICAL_PROFILE_PRINT_INTERVAL_MS, 0U, false
};

/* 函数说明：周期读取并打印一次临界区 profiling 统计。 */
static void osal_example_critical_profile_task(void *arg) {
    osal_cortexm_profile_stats_t stats;
    uint32_t now_ms;

    (void)arg;
    if (!osal_cortexm_profile_get_stats(&stats)) {
        return;
    }

    now_ms = osal_timer_get_tick();
    if (!osal_example_periodic_is_due(&s_example_profile_period, now_ms)) {
        return;
    }

    printf("critical profile: samples=%lu last=%lu cyc/%lu ns avg=%lu cyc/%lu ns\r\n",
           (unsigned long)stats.sample_count,
           (unsigned long)stats.last_cycles,
           (unsigned long)stats.last_ns,
           (unsigned long)stats.avg_cycles,
           (unsigned long)stats.avg_ns);
    osal_example_periodic_mark_run(&s_example_profile_period, now_ms);
}

/* 函数说明：初始化 IRQ profiling 打印任务。 */
void osal_example_irq_profile_demo_init(void) {
    osal_task_t *task;

    if (!osal_cortexm_profile_is_supported()) {
        return;
    }

    task = osal_task_create(
        osal_example_critical_profile_task,
        NULL,
        OSAL_TASK_PRIORITY_LOW);
    if (task != NULL) {
        (void)osal_task_start(task);
    }
}
#endif

#if OSAL_CFG_ENABLE_FLASH && OSAL_PLATFORM_ENABLE_FLASH_DEMO
/* ========================= 7. Flash 组件示例 ========================= */
/* 函数说明：把一段字节缓冲区按十六进制形式打印出来。 */
static void osal_example_flash_dump_bytes(const char *label, const uint8_t *data, uint32_t length) {
    uint32_t i;

    printf("%s", label);
    for (i = 0U; i < length; ++i) {
        printf(" %02X", data[i]);
    }
    printf("\r\n");
}

/* 函数说明：比较 Flash 回读数据是否与期望内容完全一致。 */
static bool osal_example_flash_verify(const uint8_t *expected, const uint8_t *actual, uint32_t length) {
    return (memcmp(expected, actual, length) == 0);
}

/* 函数说明：统一打印一次 Flash 写入 + 回读校验结果。 */
static void osal_example_flash_report_result(periph_flash_t *flash,
                                             const char *label,
                                             osal_status_t write_status,
                                             uint32_t address,
                                             const uint8_t *expected,
                                             uint32_t length) {
    uint8_t readback[8];
    osal_status_t read_status;

    memset(readback, 0, sizeof(readback));
    if (write_status != OSAL_OK) {
        printf("%s failed, status=%d\r\n", label, (int)write_status);
        return;
    }

    read_status = periph_flash_read(flash, address, readback, length);
    if (read_status != OSAL_OK) {
        printf("%s read failed, status=%d\r\n", label, (int)read_status);
        return;
    }

    if (!osal_example_flash_verify(expected, readback, length)) {
        printf("%s verify failed @ 0x%08lX\r\n", label, (unsigned long)address);
        osal_example_flash_dump_bytes("expect:", expected, length);
        osal_example_flash_dump_bytes("actual:", readback, length);
        return;
    }

    printf("%s ok @ 0x%08lX\r\n", label, (unsigned long)address);
    osal_example_flash_dump_bytes("readback:", readback, length);
}

/* 函数说明：Flash 示例任务，执行一次解锁、擦除、写入和回读校验。 */
static void osal_example_flash_demo_task(void *arg) {
    osal_example_flash_demo_ctx_t *ctx = (osal_example_flash_demo_ctx_t *)arg;
    uint32_t base = OSAL_PLATFORM_FLASH_DEMO_ADDRESS;
    uint32_t value32 = 0x1234A5F0UL;
    uint8_t expected32[sizeof(value32)];

    if ((ctx == NULL) || (ctx->flash == NULL) || ctx->done) {
        return;
    }

    /* 只跑一次，避免示例任务反复擦写同一片扇区。 */
    ctx->done = true;
    printf("flash demo start @ 0x%08lX\r\n", (unsigned long)base);
    if (periph_flash_unlock(ctx->flash) != OSAL_OK) {
        printf("flash unlock failed\r\n");
        return;
    }
    if (periph_flash_erase(ctx->flash, base, 0x80U) != OSAL_OK) {
        printf("flash erase failed\r\n");
        (void)periph_flash_lock(ctx->flash);
        return;
    }

    /* 先把 32 位值展开成字节数组，后面按原始字节做回读校验。 */
    memcpy(expected32, &value32, sizeof(value32));
    osal_example_flash_report_result(
        ctx->flash,
        "flash u32",
        periph_flash_write_u32(ctx->flash, base, value32),
        base,
        expected32,
        (uint32_t)sizeof(expected32));
    (void)periph_flash_lock(ctx->flash);
}

/* 函数说明：初始化一次性的 Flash 示例任务。 */
void osal_example_flash_demo_init(void) {
    osal_task_t *task;

    s_example_flash = osal_platform_flash_create();
    if (s_example_flash == NULL) {
        return;
    }

    s_example_flash_demo_ctx.flash = s_example_flash;
    s_example_flash_demo_ctx.done = false;
    task = osal_task_create(
        osal_example_flash_demo_task,
        &s_example_flash_demo_ctx,
        OSAL_TASK_PRIORITY_LOW);
    if (task != NULL) {
        (void)osal_task_start(task);
    }
}
#endif
