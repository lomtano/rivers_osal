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

typedef struct {
    uint32_t sequence;
    uint8_t payload[8];
} queue_message_t;

static periph_uart_t *g_uart = NULL;
#ifdef OSAL_PLATFORM_ENABLE_FLASH_DEMO
static periph_flash_t *g_flash = NULL;
#endif
static queue_message_t g_queue_storage[8];
static led_task_ctx_t g_led1_ctx = {200U, 0U, osal_platform_led1_toggle};
static led_task_ctx_t g_led2_ctx = {500U, 0U, osal_platform_led2_toggle};
static queue_producer_ctx_t g_queue_producer_ctx;
static queue_consumer_ctx_t g_queue_consumer_ctx;
#ifdef OSAL_PLATFORM_ENABLE_FLASH_DEMO
static flash_demo_ctx_t g_flash_demo_ctx;
#endif

/* 通过平台层注册好的 USART 组件重定向 printf。 */
int fputc(int ch, FILE *f) {
    return periph_uart_fputc(ch, f);
}

/* 每个点灯任务都按自己的周期翻转 LED，不阻塞调度器。 */
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

/* 周期性往队列里投递一个结构体消息。 */
static void queue_producer_task(void *arg) {
    queue_producer_ctx_t *ctx = (queue_producer_ctx_t *)arg;
    uint32_t now = osal_timer_get_tick();
    queue_message_t message;
    uint32_t i;

    if (ctx == NULL) {
        return;
    }

    if ((int32_t)(now - ctx->next_tick) >= 0) {
        message.sequence = ctx->count++;
        for (i = 0U; i < (uint32_t)sizeof(message.payload); ++i) {
            message.payload[i] = (uint8_t)(message.sequence + i);
        }

        if (osal_queue_send(ctx->queue, &message) == OSAL_OK) {
            printf("queue send: seq=%lu first=%u count=%lu\r\n",
                   (unsigned long)message.sequence,
                   (unsigned int)message.payload[0],
                   (unsigned long)osal_queue_get_count(ctx->queue));
        }
        ctx->next_tick = now + 1000U;
    }
}

/* 非阻塞轮询队列，并打印取出的结构体消息。 */
static void queue_consumer_task(void *arg) {
    queue_consumer_ctx_t *ctx = (queue_consumer_ctx_t *)arg;
    queue_message_t message;

    if (ctx == NULL) {
        return;
    }

    if (osal_queue_recv(ctx->queue, &message) == OSAL_OK) {
        printf("queue recv: seq=%lu bytes=%u,%u count=%lu\r\n",
               (unsigned long)message.sequence,
               (unsigned int)message.payload[0],
               (unsigned int)message.payload[1],
               (unsigned long)osal_queue_get_count(ctx->queue));
    }
}

/* 单次软件定时器回调示例。 */
static void oneshot_timer_callback(void *arg) {
    (void)arg;
    printf("single timer fired\r\n");
}

/* 周期性软件定时器回调示例。 */
static void periodic_timer_callback(void *arg) {
    static uint32_t count = 0U;

    (void)arg;
    printf("periodic timer: %lu\r\n", (unsigned long)++count);
}

#ifdef OSAL_PLATFORM_ENABLE_FLASH_DEMO
/* 上电后仅执行一次 Flash 擦写回读演示。 */
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

/* 创建两个无阻塞点灯任务。 */
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

/* 创建一个静态缓存区队列，以及配套的生产者/消费者任务。 */
static void app_queue_demo_init(void) {
    osal_queue_t *queue = osal_queue_create_static(g_queue_storage, 8U, (uint32_t)sizeof(queue_message_t));
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

/* 创建一个单次定时器和一个周期定时器，用于串口打印演示。 */
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
/* 创建可选 Flash 示例任务，启用前请先预留安全扇区。 */
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

/* STM32F4 集成入口示例。 */
int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    OSAL_PLATFORM_UART_INIT();

    osal_platform_init();
    osal_platform_tick_start();

    g_uart = osal_platform_uart_create();
    if (g_uart != NULL) {
        (void)periph_uart_bind_console(g_uart);
        printf("\r\nOSAL STM32F4 demo\r\n");
        printf("tick source: 1us irq -> osal_timer_inc_tick()\r\n");
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

/* 中断里只做转发，让平台适配和业务示例保持分层。 */
void TIM2_IRQHandler(void) {
    osal_platform_tick_irq_handler();
}
