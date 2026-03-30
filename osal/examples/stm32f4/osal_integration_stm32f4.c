#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "osal.h"
#include "periph_uart.h"
#include "periph_flash.h"
#include "osal_platform_stm32f4.h"

/*
 * 说明：
 * 1. 这个文件是“如何使用 OSAL”的纯示范文件，默认不建议加入正式工程编译。
 * 2. main.c 可以视作你的应用层；这里的代码更像示例库，方便你按需复制到 main.c 中。
 * 3. 适配层相关内容不放在这里，本文件只演示任务、队列、软件定时器和组件的使用方式。
 */

typedef struct {
    uint32_t interval_ms;
    void (*toggle)(void);
} osal_example_led_ctx_t;

typedef struct {
    osal_queue_t *queue;
    uint32_t sequence;
} osal_example_queue_producer_ctx_t;

typedef struct {
    osal_queue_t *queue;
} osal_example_queue_consumer_ctx_t;

typedef struct {
    uint32_t sequence;
    uint8_t payload[8];
} osal_example_queue_message_t;

static periph_uart_t *s_example_uart = NULL;
static periph_flash_t *s_example_flash = NULL;
static osal_example_queue_message_t s_example_queue_storage[8];
static osal_example_led_ctx_t s_led1_ctx = {500U, osal_platform_led1_toggle};
static osal_example_led_ctx_t s_led2_ctx = {1000U, osal_platform_led2_toggle};
static osal_example_queue_producer_ctx_t s_producer_ctx;
static osal_example_queue_consumer_ctx_t s_consumer_ctx;

/* ========================= 控制台绑定示例 ========================= */

void osal_example_bind_console(void) {
    s_example_uart = osal_platform_uart_create();
    if (s_example_uart != NULL) {
        (void)periph_uart_bind_console(s_example_uart);
    }
}

/* ========================= 任务示例：两个点灯任务 ========================= */

static void osal_example_led_task(void *arg) {
    osal_example_led_ctx_t *ctx = (osal_example_led_ctx_t *)arg;

    if ((ctx == NULL) || (ctx->toggle == NULL)) {
        return;
    }

    ctx->toggle();
    (void)osal_task_sleep(NULL, ctx->interval_ms);
}

void osal_example_led_demo_init(void) {
    osal_task_t *led1_task = osal_task_create(
        osal_example_led_task,
        &s_led1_ctx,
        OSAL_TASK_PRIORITY_LOW);
    osal_task_t *led2_task = osal_task_create(
        osal_example_led_task,
        &s_led2_ctx,
        OSAL_TASK_PRIORITY_LOW);

    if (led1_task != NULL) {
        (void)osal_task_start(led1_task);
    }
    if (led2_task != NULL) {
        (void)osal_task_start(led2_task);
    }
}

/* ========================= 队列示例：高优先级生产者/消费者 ========================= */

static void osal_example_queue_producer_task(void *arg) {
    osal_example_queue_producer_ctx_t *ctx = (osal_example_queue_producer_ctx_t *)arg;
    osal_example_queue_message_t message;
    uint32_t i;

    if ((ctx == NULL) || (ctx->queue == NULL)) {
        return;
    }

    message.sequence = ctx->sequence++;
    for (i = 0U; i < (uint32_t)sizeof(message.payload); ++i) {
        message.payload[i] = (uint8_t)(message.sequence + i);
    }

    if (osal_queue_send(ctx->queue, &message) == OSAL_OK) {
        printf("queue send: seq=%lu first=%u count=%lu\r\n",
               (unsigned long)message.sequence,
               (unsigned int)message.payload[0],
               (unsigned long)osal_queue_get_count(ctx->queue));
    }

    (void)osal_task_sleep(NULL, 1000U);
}

static void osal_example_queue_consumer_task(void *arg) {
    osal_example_queue_consumer_ctx_t *ctx = (osal_example_queue_consumer_ctx_t *)arg;
    osal_example_queue_message_t message;

    if ((ctx == NULL) || (ctx->queue == NULL)) {
        return;
    }

    if (osal_queue_recv(ctx->queue, &message) == OSAL_OK) {
        printf("queue recv: seq=%lu bytes=%u,%u count=%lu\r\n",
               (unsigned long)message.sequence,
               (unsigned int)message.payload[0],
               (unsigned int)message.payload[1],
               (unsigned long)osal_queue_get_count(ctx->queue));
        return;
    }

    (void)osal_task_sleep(NULL, 10U);
}

void osal_example_queue_demo_init(void) {
    osal_queue_t *queue = osal_queue_create_static(
        s_example_queue_storage,
        8U,
        (uint32_t)sizeof(osal_example_queue_message_t));
    osal_task_t *producer_task;
    osal_task_t *consumer_task;

    if (queue == NULL) {
        printf("queue create failed\r\n");
        return;
    }

    s_producer_ctx.queue = queue;
    s_producer_ctx.sequence = 0U;
    s_consumer_ctx.queue = queue;

    producer_task = osal_task_create(
        osal_example_queue_producer_task,
        &s_producer_ctx,
        OSAL_TASK_PRIORITY_HIGH);
    consumer_task = osal_task_create(
        osal_example_queue_consumer_task,
        &s_consumer_ctx,
        OSAL_TASK_PRIORITY_HIGH);

    if (producer_task != NULL) {
        (void)osal_task_start(producer_task);
    }
    if (consumer_task != NULL) {
        (void)osal_task_start(consumer_task);
    }
}

/* ========================= 软件定时器示例 ========================= */

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

/* ========================= RTT 任务示例 ========================= */

void osal_example_rtt_task(void *arg) {
    static bool s_rtt_initialized = false;

    (void)arg;
    if (!s_rtt_initialized) {
        SEGGER_RTT_Init();
        s_rtt_initialized = true;
    }

    LOGI("rtt task running: %lu ms\r\n", (unsigned long)osal_timer_get_tick());
    (void)osal_task_sleep(NULL, 500U);
}

void osal_example_rtt_demo_init(void) {
    osal_task_t *task = osal_task_create(
        osal_example_rtt_task,
        NULL,
        OSAL_TASK_PRIORITY_MEDIUM);

    if (task != NULL) {
        (void)osal_task_start(task);
    }
}

/* ========================= Flash 组件示例 ========================= */

void osal_example_flash_demo_once(void) {
    uint8_t payload[] = {0x52U, 0x56U, 0x4FU, 0x53U, 0x01U, 0x02U, 0x03U, 0x04U};
    uint8_t readback[sizeof(payload)];

    s_example_flash = osal_platform_flash_create();
    if (s_example_flash == NULL) {
        printf("flash component create failed\r\n");
        return;
    }

    memset(readback, 0, sizeof(readback));
    printf("flash demo start @ 0x%08lX\r\n", (unsigned long)OSAL_PLATFORM_FLASH_DEMO_ADDRESS);

    if (periph_flash_unlock(s_example_flash) != OSAL_OK) {
        printf("flash unlock failed\r\n");
        return;
    }

    if (periph_flash_erase(s_example_flash, OSAL_PLATFORM_FLASH_DEMO_ADDRESS, sizeof(payload)) != OSAL_OK) {
        printf("flash erase failed\r\n");
        (void)periph_flash_lock(s_example_flash);
        return;
    }

    if (periph_flash_write(s_example_flash, OSAL_PLATFORM_FLASH_DEMO_ADDRESS, payload, sizeof(payload)) != OSAL_OK) {
        printf("flash write failed\r\n");
        (void)periph_flash_lock(s_example_flash);
        return;
    }

    if (periph_flash_read(s_example_flash, OSAL_PLATFORM_FLASH_DEMO_ADDRESS, readback, sizeof(readback)) != OSAL_OK) {
        printf("flash read failed\r\n");
        (void)periph_flash_lock(s_example_flash);
        return;
    }

    (void)periph_flash_lock(s_example_flash);
    printf("flash readback: %02X %02X %02X %02X\r\n",
           readback[0], readback[1], readback[2], readback[3]);
}

/* ========================= 一键启动示例 ========================= */

void osal_example_start_all(void) {
    osal_example_bind_console();
    osal_example_led_demo_init();
    osal_example_queue_demo_init();
    osal_example_timer_demo_init();
    osal_example_rtt_demo_init();
}
