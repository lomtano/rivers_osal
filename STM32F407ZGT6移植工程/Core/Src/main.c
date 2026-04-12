/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* 如需启用 Flash 示例，请在包含 osal.h 之前先把下面这个宏改成 1。
 * 之所以要放在 osal.h 前面，是因为 osal.h 会继续聚合平台头，
 * 而平台头里的部分示例配置会读取这个宏。 */
/* #define OSAL_PLATFORM_ENABLE_FLASH_DEMO 1 */
//#define OSAL_PLATFORM_ENABLE_FLASH_DEMO 1
#include "osal.h"
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct
{
  uint32_t interval_ms;   /* 这个 LED 任务每次翻转后要休眠多久。单位：毫秒。 */
  void (*toggle)(void);   /* 真正翻转 LED 的函数指针，由平台层提供。 */
} led_task_ctx_t;

#if OSAL_CFG_ENABLE_FLASH && OSAL_PLATFORM_ENABLE_FLASH_DEMO
typedef struct
{
  periph_flash_t *flash;  /* 本次 Flash 测试要操作的组件对象。 */
  bool done;              /* 只执行一次测试，避免每轮调度都重复擦写 Flash。 */
} flash_demo_ctx_t;
#endif

typedef struct
{
  uint32_t sequence;   /* 这是第几条消息，用于观察收发顺序。 */
  uint8_t payload[8];  /* 一段固定长度负载，用来演示结构体消息队列。 */
} queue_message_t;
static uint32_t g_queue_sequence = 0U;    /* 发送任务每发一条消息就自增一次。 */

static osal_queue_t *g_demo_queue = NULL; /* main.c 队列示例共用的队列对象。 */

/* 同一个任务函数配合不同参数，就能生成两个行为不同的 LED 任务。
 * 这是协作式任务里非常常见的复用方式。 */
static led_task_ctx_t g_led1_ctx = {500U, osal_platform_led1_toggle};
static led_task_ctx_t g_led2_ctx = {1000U, osal_platform_led2_toggle};

#if OSAL_CFG_ENABLE_FLASH && OSAL_PLATFORM_ENABLE_FLASH_DEMO
static periph_flash_t *g_flash = NULL;
#endif
#if OSAL_CFG_ENABLE_FLASH && OSAL_PLATFORM_ENABLE_FLASH_DEMO
static flash_demo_ctx_t g_flash_demo_ctx;
#endif

static periph_uart_t *s_example_uart = NULL; /* 绑定到 printf 的控制台串口对象。 */

/* fputc 是标准库输出链路最底层的“输出一个字符”接口。
 * 把它转给 periph_uart_fputc() 之后，printf 最终就会走到 OSAL 的 USART 组件。 */
int fputc(int ch, FILE *f)
{
  return periph_uart_fputc(ch, f);
}

/* 单次软件定时器回调示例。
 * 它只会执行一次，用来观察“单次定时器是否按时触发”。 */
static void oneshot_timer_callback(void *arg)
{
  (void)arg;
  uint32_t now = osal_timer_get_tick();
  LOGE("oneshot timer: %lu\r\n", (unsigned long)now);
}

/* 周期软件定时器回调示例。
 * 它会周期性执行，用来观察软件定时器的节拍是否稳定。 */
static void periodic_timer_callback(void *arg)
{
  (void)arg;
  uint32_t now = osal_timer_get_tick();
  LOGI("periodic timer: %lu\r\n", (unsigned long)now);
}

/* 点灯任务示例。
 * 这个任务每次被调度到时只做两件事：
 * 1. 翻转一次 LED。
 * 2. 让自己休眠一个周期。
 *
 * 这样写的好处是：
 * 1. 单次执行足够短。
 * 2. 不会长时间占住 CPU。
 * 3. 很符合协作式任务“做一点就 return”的习惯。 */
static void led_task(void *arg)
{
  led_task_ctx_t *ctx = (led_task_ctx_t *)arg;

  if ((ctx == NULL) || (ctx->toggle == NULL))
  {
    return;
  }
//  uint32_t now = osal_timer_get_tick();
//  LOGE("led timer: %lu\r\n", (unsigned long)now);
  /* 真正翻转哪一个 LED，不在这里写死，而是由参数里的函数指针决定。 */
  ctx->toggle();
  /* 这里不是忙等延时，而是把当前任务挂起到指定毫秒后再恢复。 */
  (void)osal_task_sleep(NULL, ctx->interval_ms);
}

/* 队列发送任务。
 * 这段代码重点演示三件事：
 * 1. 队列消息项可以直接定义成结构体。
 * 2. 队列满时，不需要 while 死循环轮询，可以直接进入真正阻塞。
 * 3. 一旦接收方取走消息，等待发送的任务会被事件驱动唤醒。 */
static void queue_producer_task(void *arg)
{
  queue_message_t message;
  uint32_t i;
  osal_status_t status;

  (void)arg;
  if (g_demo_queue == NULL)
  {
    return;
  }

  /* 先给这条消息分配一个自增序号。 */
  message.sequence = g_queue_sequence++;
  /* 再把 payload 填成“序号 + 偏移”的形式，方便接收侧观察数据变化。 */
  for (i = 0U; i < (uint32_t)sizeof(message.payload); ++i)
  {
    message.payload[i] = (uint8_t)(message.sequence + i);
  }

  /* 如果队列已满，这里不会 while 轮询，而是把当前任务挂起。
   * 等到队列出现空位后，系统会再把这个任务唤醒。 */
  status = osal_queue_send_timeout(g_demo_queue, &message, OSAL_WAIT_FOREVER);
  if (status == OSAL_OK)
  {
//        printf("queue send: seq=%lu first=%u count=%lu\r\n",
//               (unsigned long)message.sequence,
//               (unsigned int)message.payload[0],
//               (unsigned long)osal_queue_get_count(g_demo_queue));
  }
  else if (status == OSAL_ERR_BLOCKED)
  {
    /*
     * 这里不是“发送失败”，而是当前任务已经因为队列满进入 BLOCKED。
     * 这种情况下必须立刻 return，不能继续往下执行别的 sleep/sleep_until，
     * 否则会把刚建立好的等待状态覆盖掉。
     */
    return;
  }
  else if (status == OSAL_ERR_DELETED)
  {
    printf("queue send aborted: queue deleted\r\n");
    return;
  }

  /* 这个发送任务是典型的周期任务，所以用 sleep_until 比 sleep 更稳。 */
  (void)osal_task_sleep_until(NULL, 1000U);
}

/* 队列接收任务。
 * 这个任务用来配合发送任务演示：
 * 1. 没有消息时，接收任务会进入 BLOCKED，而不是占着 CPU 不放。
 * 2. 一旦队列里出现新消息，等待接收的任务会被主动唤醒。
 * 3. 队列等待已经是事件驱动的，不再是单纯轮询。 */
static void queue_consumer_task(void *arg)
{
  queue_message_t message;

  (void)arg;
  if (g_demo_queue == NULL)
  {
    return;
  }

  /* 如果当前队列为空，这里会把任务挂起到“等待可读”链表里。 */
  {
    osal_status_t status = osal_queue_recv_timeout(g_demo_queue, &message, OSAL_WAIT_FOREVER);
    if (status == OSAL_OK)
    {
//        printf("queue recv: seq=%lu bytes=%u,%u count=%lu\r\n",
//               (unsigned long)message.sequence,
//               (unsigned int)message.payload[0],
//               (unsigned int)message.payload[1],
//               (unsigned long)osal_queue_get_count(g_demo_queue));
    }
    else if (status == OSAL_ERR_BLOCKED)
    {
      return;
    }
    else if (status == OSAL_ERR_DELETED)
    {
      printf("queue recv aborted: queue deleted\r\n");
      return;
    }
  }
}

/* RTT 示例任务：每 500ms 打印一次当前 tick。
 * 它非常适合用来观察任务周期有没有明显漂移。 */
void SEGGER_RTT_Test(void *arg)
{
  static bool s_rtt_initialized = false;
  (void)arg;
  if (!s_rtt_initialized)
  {
    SEGGER_RTT_Init();
    s_rtt_initialized = true;
  }

  LOGW("rtt task running: %lu ms\r\n", (unsigned long)osal_timer_get_tick());
//		(void)osal_task_sleep_until(NULL, 500U);
  (void)osal_task_sleep(NULL, 500U);
}

#if OSAL_CFG_ENABLE_FLASH && OSAL_PLATFORM_ENABLE_FLASH_DEMO
/* 打印一段 Flash 回读数据。
 * 一旦写入失败或校验失败，串口里可以直接看到原始字节。 */
static void flash_demo_dump_bytes(const char *label, const uint8_t *data, uint32_t length)
{
  uint32_t i;

  printf("%s", label);
  for (i = 0U; i < length; ++i)
  {
    printf(" %02X", data[i]);
  }
  printf("\r\n");
}

/* 校验 Flash 回读内容是否与预期一致。 */
static bool flash_demo_verify(const uint8_t *expected, const uint8_t *actual, uint32_t length)
{
  return (memcmp(expected, actual, length) == 0);
}

/* 执行一次“写入 + 回读 + 比对”的完整位宽测试。
 * 这样每种位宽都能复用同一套结果输出逻辑。 */
static void flash_demo_report_result(periph_flash_t *flash,
                                     const char *label,
                                     osal_status_t write_status,
                                     uint32_t address,
                                     const uint8_t *expected,
                                     uint32_t length)
{
  uint8_t readback[8];
  osal_status_t read_status;

  memset(readback, 0, sizeof(readback));

  /* 如果底层写入函数本身就失败了，这里不再继续做无意义的回读。 */
  if (write_status != OSAL_OK)
  {
    printf("%s failed, status=%d\r\n", label, (int)write_status);
    return;
  }

  read_status = periph_flash_read(flash, address, readback, length);
  if (read_status != OSAL_OK)
  {
    printf("%s read failed, status=%d\r\n", label, (int)read_status);
    return;
  }

  if (!flash_demo_verify(expected, readback, length))
  {
    printf("%s verify failed @ 0x%08lX\r\n", label, (unsigned long)address);
    flash_demo_dump_bytes("expect:", expected, length);
    flash_demo_dump_bytes("actual:", readback, length);
    return;
  }

  printf("%s ok @ 0x%08lX\r\n", label, (unsigned long)address);
  flash_demo_dump_bytes("readback:", readback, length);
}

/* 测试 8 位写入接口。 */
static void flash_demo_test_u8(periph_flash_t *flash, uint32_t address)
{
  uint8_t value = 0xA5U;
  osal_status_t status = periph_flash_write_u8(flash, address, value);

  flash_demo_report_result(flash, "flash u8", status, address, &value, sizeof(value));
}

/* 测试 16 位写入接口。 */
static void flash_demo_test_u16(periph_flash_t *flash, uint32_t address)
{
  uint16_t value = 0x5AA5U;
  uint8_t expected[sizeof(value)];
  osal_status_t status;

  memcpy(expected, &value, sizeof(value));
  status = periph_flash_write_u16(flash, address, value);
  flash_demo_report_result(flash, "flash u16", status, address, expected, sizeof(expected));
}

/* 测试 32 位写入接口。 */
static void flash_demo_test_u32(periph_flash_t *flash, uint32_t address)
{
  uint32_t value = 0x1234A5F0UL;
  uint8_t expected[sizeof(value)];
  osal_status_t status;

  memcpy(expected, &value, sizeof(value));
  status = periph_flash_write_u32(flash, address, value);
  flash_demo_report_result(flash, "flash u32", status, address, expected, sizeof(expected));
}

/* 测试 64 位写入接口。 */
static void flash_demo_test_u64(periph_flash_t *flash, uint32_t address)
{
  uint64_t value = 0x1122334455667788ULL;
  uint8_t expected[sizeof(value)];
  osal_status_t status;

  memcpy(expected, &value, sizeof(value));
  status = periph_flash_write_u64(flash, address, value);
  flash_demo_report_result(flash, "flash u64", status, address, expected, sizeof(expected));
}

/* Flash 示例任务。
 * 它会依次测试 u8 / u16 / u32 / u64 四种写入接口，
 * 帮助用户直接判断当前芯片在当前板级条件下到底支持哪种位宽。 */
static void flash_demo_task(void *arg)
{
  flash_demo_ctx_t *ctx = (flash_demo_ctx_t *)arg;
  uint32_t base = OSAL_PLATFORM_FLASH_DEMO_ADDRESS;

  if ((ctx == NULL) || ctx->done)
  {
    return;
  }

  ctx->done = true;

  /* 先解锁，再擦除，再逐个测试不同位宽。 */
  printf("flash demo start @ 0x%08lX\r\n", (unsigned long)base);
  if (periph_flash_unlock(ctx->flash) != OSAL_OK)
  {
    printf("flash unlock failed\r\n");
    return;
  }

  if (periph_flash_erase(ctx->flash, base, 0x80U) != OSAL_OK)
  {
    printf("flash erase failed\r\n");
    (void)periph_flash_lock(ctx->flash);
    return;
  }

  flash_demo_test_u8(ctx->flash, base + 0x00U);
  flash_demo_test_u16(ctx->flash, base + 0x10U);
  flash_demo_test_u32(ctx->flash, base + 0x20U);
  flash_demo_test_u64(ctx->flash, base + 0x30U);

  (void)periph_flash_lock(ctx->flash);
}
#endif

/* 初始化两个低优先级点灯任务。
 * 这两个任务都很轻，只负责“背景效果”，所以适合放低优先级。 */
static void app_led_demo_init(void)
{
  osal_task_t *led1_task;
  osal_task_t *led2_task;

  led1_task = osal_task_create(led_task, &g_led1_ctx, OSAL_TASK_PRIORITY_LOW);
  led2_task = osal_task_create(led_task, &g_led2_ctx, OSAL_TASK_PRIORITY_LOW);
  if (led1_task != NULL)
  {
    (void)osal_task_start(led1_task);
  }
  if (led2_task != NULL)
  {
    (void)osal_task_start(led2_task);
  }
}

/* 初始化队列收发示例：
 * 1. 队列本身通过 osal_mem 统一内存池创建。
 * 2. 一个高优先级任务发送结构体消息。
 * 3. 一个高优先级任务接收结构体消息。
 *
 * 等待语义：
 * - 0U                不等待
 * - N 毫秒            最多等待 N 毫秒
 * - OSAL_WAIT_FOREVER 一直等
 */
static void app_queue_demo_init(void)
{
  osal_task_t *producer_task;
  osal_task_t *consumer_task;

  g_demo_queue = osal_queue_create(8U, (uint32_t)sizeof(queue_message_t));
  if (g_demo_queue == NULL)
  {
    printf("queue create failed\r\n");
    return;
  }

  producer_task = osal_task_create(queue_producer_task, NULL, OSAL_TASK_PRIORITY_HIGH);
  consumer_task = osal_task_create(queue_consumer_task, NULL, OSAL_TASK_PRIORITY_HIGH);
  if (producer_task != NULL)
  {
    (void)osal_task_start(producer_task);
  }
  if (consumer_task != NULL)
  {
    (void)osal_task_start(consumer_task);
  }
}

/* 初始化单次与周期软件定时器示例。 */
static void app_timer_demo_init(void)
{
  int oneshot_timer = osal_timer_create(2000000U, false, oneshot_timer_callback, NULL);
  int periodic_timer = osal_timer_create(1000000U, true, periodic_timer_callback, NULL);

  if (oneshot_timer >= 0)
  {
    (void)osal_timer_start(oneshot_timer);
  }
  if (periodic_timer >= 0)
  {
    (void)osal_timer_start(periodic_timer);
  }
}

/* 初始化 RTT 周期打印任务。 */
static void app_rtt_demo_init(void)
{
  osal_task_t *rtt_task = osal_task_create(SEGGER_RTT_Test, NULL, OSAL_TASK_PRIORITY_MEDIUM);

  if (rtt_task != NULL)
  {
    (void)osal_task_start(rtt_task);
  }
}

#if OSAL_CFG_ENABLE_FLASH && OSAL_PLATFORM_ENABLE_FLASH_DEMO
/* 初始化可选的 Flash 示例任务。
 * 这里只负责创建测试任务，不在初始化阶段直接擦写 Flash。 */
static void app_flash_demo_init(void)
{
  osal_task_t *task;

  g_flash_demo_ctx.flash = g_flash;
  g_flash_demo_ctx.done = false;
  task = osal_task_create(flash_demo_task, &g_flash_demo_ctx, OSAL_TASK_PRIORITY_LOW);
  if (task != NULL)
  {
    (void)osal_task_start(task);
  }
}
#endif


static osal_event_t *s_example_event = NULL; /* 事件示例共用的事件对象。 */

/* 事件等待任务。
 * 没有事件时，任务会等待；事件到来时，任务会被唤醒。 */
static void osal_example_event_wait_task(void *arg)
{
  osal_status_t status;

  (void)arg;

  if (s_example_event == NULL)
  {
    return;
  }

  status = osal_event_wait(s_example_event, 1000U);
  if (status == OSAL_OK)
  {
    printf("event wait ok\r\n");
  }
  else if (status == OSAL_ERR_TIMEOUT)
  {
    printf("event wait timeout\r\n");
  }
  else if (status == OSAL_ERR_BLOCKED)
  {
    /*
     * 这里表示当前任务已经因为“等待事件触发”进入 BLOCKED。
     * 一旦走到这个分支，就必须立即 return，不能再执行后面的 sleep，
     * 否则会覆盖掉事件等待状态。
     */
    return;
  }
  else if (status == OSAL_ERR_DELETED)
  {
    printf("event wait aborted: event deleted\r\n");
    return;
  }

  (void)osal_task_sleep(NULL, 100U);
}

/* 事件置位任务。
 * 它每 1000ms 置位一次事件，驱动等待任务继续运行。 */
static void osal_example_event_set_task(void *arg)
{
  (void)arg;

  if (s_example_event == NULL)
  {
    return;
  }

  (void)osal_event_set(s_example_event);
  (void)osal_task_sleep(NULL, 1000U);
}

/* 初始化事件示例。 */
void osal_example_event_demo_init(void)
{
  osal_task_t *wait_task;
  osal_task_t *set_task;

  s_example_event = osal_event_create(true);
  if (s_example_event == NULL)
  {
    return;
  }

  wait_task = osal_task_create(osal_example_event_wait_task, NULL, OSAL_TASK_PRIORITY_MEDIUM);
  set_task = osal_task_create(osal_example_event_set_task, NULL, OSAL_TASK_PRIORITY_MEDIUM);
  if (wait_task != NULL)
  {
    (void)osal_task_start(wait_task);
  }
  if (set_task != NULL)
  {
    (void)osal_task_start(set_task);
  }
}

/* 初始化 USART 控制台示例。
 * 这段代码演示：
 * 1. 如何创建平台串口对象；
 * 2. 如何绑定成 printf 控制台；
 * 3. 如何混合使用 printf 和 periph_uart_write_xxx。 */
void osal_example_usart_demo_init(void)
{
  static const uint8_t raw_bytes[] = {'a', 'b', 'c', '\r', '\n'};

  s_example_uart = osal_platform_uart_create();
  if (s_example_uart == NULL)
  {
    return;
  }

  (void)periph_uart_bind_console(s_example_uart);
  printf("\r\nOSAL STM32F4 demo\r\n");
  printf("tick source: osal_init() + SysTick counter\r\n");
  (void)periph_uart_write_string(s_example_uart, "osal usart demo\r\n");
  (void)periph_uart_write(s_example_uart, raw_bytes, (uint32_t)sizeof(raw_bytes));
}
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* 初始化板级工程自身的基础外设。 */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

  /* 下面开始接入 OSAL。
   * 推荐顺序是：
   * 1. 先 osal_init()。
   * 2. 再初始化你想启用的示例任务和组件。
   * 3. 最后在 while(1) 里持续调用 osal_run()。 */
  osal_init();
  osal_example_usart_demo_init();
  app_led_demo_init();
  app_queue_demo_init();
  app_timer_demo_init();
  app_rtt_demo_init();
  osal_example_event_demo_init();

#if OSAL_CFG_ENABLE_FLASH && OSAL_PLATFORM_ENABLE_FLASH_DEMO
  g_flash = osal_platform_flash_create();
  app_flash_demo_init();
#endif

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    osal_run();
    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM12 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM12)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
