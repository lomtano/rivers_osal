#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "gpio.h"
#include "usart.h"
#include "osal.h"
#include "periph_uart.h"
#include "periph_flash.h"
#include "osal_platform_stm32f4.h"

void SystemClock_Config(void);

typedef struct {
    uint32_t interval_ms;
    uint32_t next_tick;
    void (*toggle)(void);
} led_task_ctx_t;

typedef struct {
    osal_queue_t *queue;
    uint32_t next_tick;
    uint32_t count;
} queue_producer_ctx_t;

typedef struct {
    osal_queue_t *queue;
} queue_consumer_ctx_t;

#ifdef OSAL_PLATFORM_ENABLE_FLASH_DEMO
typedef struct {
    periph_flash_t *flash;
    bool done;
} flash_demo_ctx_t;
#endif

static periph_uart_t *g_uart = NULL;
#ifdef OSAL_PLATFORM_ENABLE_FLASH_DEMO
static periph_flash_t *g_flash = NULL;
#endif
static uint32_t g_queue_storage[8];
static led_task_ctx_t g_led1_ctx = {200U, 0U, osal_platform_led1_toggle};
static led_task_ctx_t g_led2_ctx = {500U, 0U, osal_platform_led2_toggle};
static queue_producer_ctx_t g_queue_producer_ctx;
static queue_consumer_ctx_t g_queue_consumer_ctx;
#ifdef OSAL_PLATFORM_ENABLE_FLASH_DEMO
static flash_demo_ctx_t g_flash_demo_ctx;
#endif

/* Retarget printf through the UART component registered by the platform layer. */
int fputc(int ch, FILE *f) {
    return periph_uart_fputc(ch, f);
}

/* Toggle one LED at its own interval without blocking the scheduler. */
static void led_task(void *arg) {
    led_task_ctx_t *ctx = (led_task_ctx_t *)arg;
    uint32_t now = osal_timer_get_tick();

    if ((ctx == NULL) || (ctx->toggle == NULL)) {
        return;
    }

    if ((int32_t)(now - ctx->next_tick) >= 0) {
        ctx->toggle();
        ctx->next_tick = now + ctx->interval_ms;
    }
}

/* Periodically push one counter value into the queue. */
static void queue_producer_task(void *arg) {
    queue_producer_ctx_t *ctx = (queue_producer_ctx_t *)arg;
    uint32_t now = osal_timer_get_tick();

    if (ctx == NULL) {
        return;
    }

    if ((int32_t)(now - ctx->next_tick) >= 0) {
        uint32_t value = ctx->count++;
        if (osal_queue_send(ctx->queue, &value) == OSAL_OK) {
            printf("queue send: %lu\r\n", (unsigned long)value);
        }
        ctx->next_tick = now + 1000U;
    }
}

/* Poll the queue in a non-blocking way and print every received item. */
static void queue_consumer_task(void *arg) {
    queue_consumer_ctx_t *ctx = (queue_consumer_ctx_t *)arg;
    uint32_t value;

    if (ctx == NULL) {
        return;
    }

    if (osal_queue_recv(ctx->queue, &value) == OSAL_OK) {
        printf("queue recv: %lu\r\n", (unsigned long)value);
    }
}

/* Demonstrate one-shot software timer output. */
static void oneshot_timer_callback(void *arg) {
    (void)arg;
    printf("oneshot timer fired\r\n");
}

/* Demonstrate periodic software timer output. */
static void periodic_timer_callback(void *arg) {
    static uint32_t count = 0U;

    (void)arg;
    printf("periodic timer: %lu\r\n", (unsigned long)++count);
}

#ifdef OSAL_PLATFORM_ENABLE_FLASH_DEMO
/* Run one flash erase/program/readback sequence once after startup. */
static void flash_demo_task(void *arg) {
    flash_demo_ctx_t *ctx = (flash_demo_ctx_t *)arg;
    uint8_t payload[] = {0x52U, 0x56U, 0x4FU, 0x53U, 0x01U, 0x02U, 0x03U, 0x04U};
    uint8_t readback[sizeof(payload)];

    if ((ctx == NULL) || ctx->done) {
        return;
    }

    ctx->done = true;
    memset(readback, 0, sizeof(readback));

    printf("flash demo start @ 0x%08lX\r\n", (unsigned long)OSAL_PLATFORM_FLASH_DEMO_ADDRESS);
    if (periph_flash_unlock(ctx->flash) != OSAL_OK) {
        printf("flash unlock failed\r\n");
        return;
    }

    if (periph_flash_erase(ctx->flash, OSAL_PLATFORM_FLASH_DEMO_ADDRESS, sizeof(payload)) != OSAL_OK) {
        printf("flash erase failed\r\n");
        (void)periph_flash_lock(ctx->flash);
        return;
    }

    if (periph_flash_write(ctx->flash, OSAL_PLATFORM_FLASH_DEMO_ADDRESS, payload, sizeof(payload)) != OSAL_OK) {
        printf("flash write failed\r\n");
        (void)periph_flash_lock(ctx->flash);
        return;
    }

    if (periph_flash_read(ctx->flash, OSAL_PLATFORM_FLASH_DEMO_ADDRESS, readback, sizeof(readback)) != OSAL_OK) {
        printf("flash read failed\r\n");
        (void)periph_flash_lock(ctx->flash);
        return;
    }

    (void)periph_flash_lock(ctx->flash);
    printf("flash readback: %02X %02X %02X %02X\r\n",
           readback[0], readback[1], readback[2], readback[3]);
}
#endif

/* Create two cooperative LED tasks that each return immediately when idle. */
static void app_led_demo_init(void) {
    osal_task_t *led1_task;
    osal_task_t *led2_task;

    g_led1_ctx.next_tick = osal_timer_get_tick() + g_led1_ctx.interval_ms;
    g_led2_ctx.next_tick = osal_timer_get_tick() + g_led2_ctx.interval_ms;

    led1_task = osal_task_create(led_task, &g_led1_ctx);
    led2_task = osal_task_create(led_task, &g_led2_ctx);
    if (led1_task != NULL) {
        (void)osal_task_start(led1_task);
    }
    if (led2_task != NULL) {
        (void)osal_task_start(led2_task);
    }
}

/* Create one queue plus producer/consumer tasks. */
static void app_queue_demo_init(void) {
    osal_queue_t *queue = osal_queue_create(g_queue_storage, 8U, sizeof(g_queue_storage[0]));
    osal_task_t *producer_task;
    osal_task_t *consumer_task;

    if (queue == NULL) {
        printf("queue create failed\r\n");
        return;
    }

    g_queue_producer_ctx.queue = queue;
    g_queue_producer_ctx.count = 0U;
    g_queue_producer_ctx.next_tick = osal_timer_get_tick() + 1000U;
    g_queue_consumer_ctx.queue = queue;

    producer_task = osal_task_create(queue_producer_task, &g_queue_producer_ctx);
    consumer_task = osal_task_create(queue_consumer_task, &g_queue_consumer_ctx);
    if (producer_task != NULL) {
        (void)osal_task_start(producer_task);
    }
    if (consumer_task != NULL) {
        (void)osal_task_start(consumer_task);
    }
}

/* Create one one-shot timer and one periodic timer for UART output. */
static void app_timer_demo_init(void) {
    int oneshot_timer = osal_timer_create(2000000U, false, oneshot_timer_callback, NULL);
    int periodic_timer = osal_timer_create(1000000U, true, periodic_timer_callback, NULL);

    if (oneshot_timer >= 0) {
        (void)osal_timer_start(oneshot_timer);
    }
    if (periodic_timer >= 0) {
        (void)osal_timer_start(periodic_timer);
    }
}

#ifdef OSAL_PLATFORM_ENABLE_FLASH_DEMO
/* Create the optional flash demo task. Enable only after reserving a safe flash sector. */
static void app_flash_demo_init(void) {
    osal_task_t *task;

    g_flash_demo_ctx.flash = g_flash;
    g_flash_demo_ctx.done = false;
    task = osal_task_create(flash_demo_task, &g_flash_demo_ctx);
    if (task != NULL) {
        (void)osal_task_start(task);
    }
}
#endif

/* Example integration entry showing how to wire OSAL into STM32 startup. */
int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART2_UART_Init();

    osal_platform_init();
    osal_platform_tick_start();

    g_uart = osal_platform_uart_create();
    if (g_uart != NULL) {
        (void)periph_uart_bind_console(g_uart);
        printf("\r\nOSAL STM32F4 integration demo\r\n");
        printf("TIM2 tick: 1us interrupt -> osal_timer_inc_tick()\r\n");
    }

    app_led_demo_init();
    app_queue_demo_init();
    app_timer_demo_init();

#ifdef OSAL_PLATFORM_ENABLE_FLASH_DEMO
    g_flash = osal_platform_flash_create();
    app_flash_demo_init();
#endif

    while (1) {
        osal_run();
    }
}

/* TIM2 only forwards into the platform adapter so integration code stays clean. */
void TIM2_IRQHandler(void) {
    osal_platform_tick_irq_handler();
}
