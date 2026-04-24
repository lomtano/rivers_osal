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
 * 原因是 osal.h 会继续聚合平台头文件，而平台头里的部分示例配置会读取这个宏。 */
/* #define OSAL_PLATFORM_ENABLE_FLASH_DEMO 1 */
#include "osal.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct {
  uint32_t interval_ms;
  uint32_t next_run_ms;
  bool initialized;
} app_periodic_ctx_t;

typedef struct {
  app_periodic_ctx_t cadence;
  void (*toggle)(void);
} app_led_demo_ctx_t;

#if OSAL_CFG_ENABLE_QUEUE
typedef struct {
  uint32_t sequence;
  uint8_t payload[8];
} app_queue_message_t;

typedef struct {
  app_periodic_ctx_t cadence;
  uint32_t next_sequence;
} app_queue_producer_ctx_t;
#endif

#if OSAL_CFG_ENABLE_FLASH && OSAL_PLATFORM_ENABLE_FLASH_DEMO
typedef struct {
  periph_flash_t *flash;
  bool done;
} app_flash_demo_ctx_t;
#endif

/* -------------------------------------------------------------------------- */
/* Shared Helpers                                                             */
/* -------------------------------------------------------------------------- */

static bool app_tick_reached(uint32_t now_ms, uint32_t deadline_ms)
{
  return ((int32_t)(now_ms - deadline_ms) >= 0);
}

static bool app_periodic_is_due(app_periodic_ctx_t *ctx, uint32_t now_ms)
{
  if ((ctx == NULL) || (ctx->interval_ms == 0U))
  {
    return false;
  }

  if (!ctx->initialized)
  {
    ctx->next_run_ms = now_ms;
    ctx->initialized = true;
  }

  return app_tick_reached(now_ms, ctx->next_run_ms);
}

static void app_periodic_mark_run(app_periodic_ctx_t *ctx, uint32_t now_ms)
{
  if ((ctx == NULL) || (ctx->interval_ms == 0U))
  {
    return;
  }

  if (!ctx->initialized)
  {
    ctx->next_run_ms = now_ms;
    ctx->initialized = true;
  }

  do
  {
    ctx->next_run_ms += ctx->interval_ms;
  } while (app_tick_reached(now_ms, ctx->next_run_ms));
}

/* -------------------------------------------------------------------------- */
/* USART Demo                                                                 */
/* -------------------------------------------------------------------------- */

#if OSAL_CFG_ENABLE_USART
static periph_uart_t *g_usart_demo_uart = NULL;

int fputc(int ch, FILE *f)
{
  return periph_uart_fputc(ch, f);
}

void app_usart_demo_init(void)
{
  static const uint8_t raw_bytes[] = {'a', 'b', 'c', '\r', '\n'};

  g_usart_demo_uart = osal_platform_uart_create();
  if (g_usart_demo_uart == NULL)
  {
    return;
  }

  (void)periph_uart_bind_console(g_usart_demo_uart);
  printf("\r\nOSAL STM32F4 demo\r\n");
  printf("tick source: osal_init() + SysTick counter\r\n");
  (void)periph_uart_write_string(g_usart_demo_uart, "osal usart demo\r\n");
  (void)periph_uart_write(g_usart_demo_uart, raw_bytes, (uint32_t)sizeof(raw_bytes));
}
#endif

/* -------------------------------------------------------------------------- */
/* LED Demo                                                                   */
/* -------------------------------------------------------------------------- */

static app_led_demo_ctx_t g_led_demo_1_ctx = {{500U, 0U, false}, osal_platform_led1_toggle};
static app_led_demo_ctx_t g_led_demo_2_ctx = {{1000U, 0U, false}, osal_platform_led2_toggle};

static void led_demo_task(void *arg)
{
  app_led_demo_ctx_t *ctx = (app_led_demo_ctx_t *)arg;
  uint32_t now_ms;

  if ((ctx == NULL) || (ctx->toggle == NULL))
  {
    return;
  }

  now_ms = osal_timer_get_tick();
  if (!app_periodic_is_due(&ctx->cadence, now_ms))
  {
    return;
  }

  ctx->toggle();
  app_periodic_mark_run(&ctx->cadence, now_ms);
}

void app_led_demo_init(void)
{
  osal_task_t *led1_task;
  osal_task_t *led2_task;

  led1_task = osal_task_create(led_demo_task, &g_led_demo_1_ctx, OSAL_TASK_PRIORITY_LOW);
  led2_task = osal_task_create(led_demo_task, &g_led_demo_2_ctx, OSAL_TASK_PRIORITY_LOW);
  if (led1_task != NULL)
  {
    (void)osal_task_start(led1_task);
  }
  if (led2_task != NULL)
  {
    (void)osal_task_start(led2_task);
  }
}

/* -------------------------------------------------------------------------- */
/* Queue Demo                                                                 */
/* -------------------------------------------------------------------------- */

#if OSAL_CFG_ENABLE_QUEUE
static osal_queue_t *g_queue_demo_queue = NULL;
static app_queue_producer_ctx_t g_queue_demo_producer_ctx = {{1000U, 0U, false}, 0U};

static void queue_demo_producer_task(void *arg)
{
  app_queue_producer_ctx_t *ctx = (app_queue_producer_ctx_t *)arg;
  app_queue_message_t message;
  uint32_t now_ms;
  uint32_t i;
  osal_status_t status;

  if ((ctx == NULL) || (g_queue_demo_queue == NULL))
  {
    return;
  }

  now_ms = osal_timer_get_tick();
  if (!app_periodic_is_due(&ctx->cadence, now_ms))
  {
    return;
  }

  message.sequence = ctx->next_sequence;
  for (i = 0U; i < (uint32_t)sizeof(message.payload); ++i)
  {
    message.payload[i] = (uint8_t)(message.sequence + i);
  }

  /*
   * 队列本体仍然是固定项大小的环形缓冲区。
   * 这里用任务层节拍去驱动发送，只做一次立即尝试，事件驱动节奏由任务自己管理。
   */
  status = osal_queue_send(g_queue_demo_queue, &message, 0U);
  if (status == OSAL_OK)
  {
    printf("queue send: seq=%lu first=%u count=%lu\r\n",
           (unsigned long)message.sequence,
           (unsigned int)message.payload[0],
           (unsigned long)osal_queue_get_count(g_queue_demo_queue));
    ++ctx->next_sequence;
    app_periodic_mark_run(&ctx->cadence, now_ms);
  }
}

static void queue_demo_consumer_task(void *arg)
{
  app_queue_message_t message;
  osal_status_t status;

  (void)arg;
  if (g_queue_demo_queue == NULL)
  {
    return;
  }

  status = osal_queue_recv(g_queue_demo_queue, &message, 0U);
  if (status == OSAL_OK)
  {
    printf("queue recv: seq=%lu bytes=%u,%u count=%lu\r\n",
           (unsigned long)message.sequence,
           (unsigned int)message.payload[0],
           (unsigned int)message.payload[1],
           (unsigned long)osal_queue_get_count(g_queue_demo_queue));
  }
}

void app_queue_demo_init(void)
{
  osal_task_t *producer_task;
  osal_task_t *consumer_task;

  g_queue_demo_queue = osal_queue_create(8U, (uint32_t)sizeof(app_queue_message_t));
  if (g_queue_demo_queue == NULL)
  {
    printf("queue create failed\r\n");
    return;
  }

  producer_task = osal_task_create(queue_demo_producer_task, &g_queue_demo_producer_ctx, OSAL_TASK_PRIORITY_HIGH);
  consumer_task = osal_task_create(queue_demo_consumer_task, NULL, OSAL_TASK_PRIORITY_HIGH);
  if (producer_task != NULL)
  {
    (void)osal_task_start(producer_task);
  }
  if (consumer_task != NULL)
  {
    (void)osal_task_start(consumer_task);
  }
}
#endif

/* -------------------------------------------------------------------------- */
/* Timer Demo                                                                 */
/* -------------------------------------------------------------------------- */

#if OSAL_CFG_ENABLE_SW_TIMER
static void oneshot_timer_callback(void *arg)
{
  (void)arg;
  LOGE("oneshot timer: %lu\r\n", (unsigned long)osal_timer_get_tick());
}

static void periodic_timer_callback(void *arg)
{
  (void)arg;
  LOGI("periodic timer: %lu\r\n", (unsigned long)osal_timer_get_tick());
}

void app_timer_demo_init(void)
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
#endif

/* -------------------------------------------------------------------------- */
/* RTT Demo                                                                   */
/* -------------------------------------------------------------------------- */

static app_periodic_ctx_t g_rtt_demo_period = {500U, 0U, false};

static void rtt_demo_task(void *arg)
{
  static bool s_rtt_initialized = false;
  uint32_t now_ms;

  (void)arg;
  if (!s_rtt_initialized)
  {
    SEGGER_RTT_Init();
    s_rtt_initialized = true;
  }

  now_ms = osal_timer_get_tick();
  if (!app_periodic_is_due(&g_rtt_demo_period, now_ms))
  {
    return;
  }

  LOGW("rtt task running: %lu ms\r\n", (unsigned long)now_ms);
  app_periodic_mark_run(&g_rtt_demo_period, now_ms);
}

void app_rtt_demo_init(void)
{
  osal_task_t *task = osal_task_create(rtt_demo_task, NULL, OSAL_TASK_PRIORITY_MEDIUM);

  if (task != NULL)
  {
    (void)osal_task_start(task);
  }
}

/* -------------------------------------------------------------------------- */
/* DWT Profile Demo                                                           */
/* -------------------------------------------------------------------------- */

#if OSAL_CFG_ENABLE_IRQ_PROFILE
static app_periodic_ctx_t g_dwt_profile_demo_period = {
  OSAL_CORTEXM_CRITICAL_PROFILE_PRINT_INTERVAL_MS, 0U, false
};

static void dwt_profile_demo_task(void *arg)
{
  osal_cortexm_profile_stats_t stats;
  uint32_t now_ms;
  uint32_t last_us;
  uint32_t min_us;
  uint32_t max_us;
  uint32_t avg_us;

  (void)arg;
  if (!osal_cortexm_profile_get_stats(&stats))
  {
    return;
  }

  now_ms = osal_timer_get_tick();
  if (!app_periodic_is_due(&g_dwt_profile_demo_period, now_ms))
  {
    return;
  }

  last_us = osal_cortexm_profile_cycles_to_us(stats.last_cycles);
  min_us = osal_cortexm_profile_cycles_to_us(stats.min_cycles);
  max_us = osal_cortexm_profile_cycles_to_us(stats.max_cycles);
  avg_us = osal_cortexm_profile_cycles_to_us(stats.avg_cycles);
  printf("dwt profile: hz=%lu samples=%lu last=%lu cyc/%lu ns/%lu us min=%lu cyc/%lu ns/%lu us max=%lu cyc/%lu ns/%lu us avg=%lu cyc/%lu ns/%lu us\r\n",
         (unsigned long)stats.cpu_clock_hz,
         (unsigned long)stats.sample_count,
         (unsigned long)stats.last_cycles,
         (unsigned long)stats.last_ns,
         (unsigned long)last_us,
         (unsigned long)stats.min_cycles,
         (unsigned long)stats.min_ns,
         (unsigned long)min_us,
         (unsigned long)stats.max_cycles,
         (unsigned long)stats.max_ns,
         (unsigned long)max_us,
         (unsigned long)stats.avg_cycles,
         (unsigned long)stats.avg_ns,
         (unsigned long)avg_us);
  app_periodic_mark_run(&g_dwt_profile_demo_period, now_ms);
}

void app_dwt_profile_demo_init(void)
{
  osal_task_t *task;

  if (!osal_cortexm_profile_is_supported())
  {
    return;
  }

  task = osal_task_create(dwt_profile_demo_task, NULL, OSAL_TASK_PRIORITY_LOW);
  if (task != NULL)
  {
    (void)osal_task_start(task);
  }
}
#endif

/* -------------------------------------------------------------------------- */
/* Flash Demo                                                                 */
/* -------------------------------------------------------------------------- */

#if OSAL_CFG_ENABLE_FLASH && OSAL_PLATFORM_ENABLE_FLASH_DEMO
static periph_flash_t *g_flash_demo_device = NULL;
static app_flash_demo_ctx_t g_flash_demo_ctx = {0};

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

static bool flash_demo_verify(const uint8_t *expected, const uint8_t *actual, uint32_t length)
{
  return (memcmp(expected, actual, length) == 0);
}

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

static void flash_demo_test_u8(periph_flash_t *flash, uint32_t address)
{
  uint8_t value = 0xA5U;
  osal_status_t status = periph_flash_write_u8(flash, address, value);

  flash_demo_report_result(flash, "flash u8", status, address, &value, sizeof(value));
}

static void flash_demo_test_u16(periph_flash_t *flash, uint32_t address)
{
  uint16_t value = 0x5AA5U;
  uint8_t expected[sizeof(value)];
  osal_status_t status;

  memcpy(expected, &value, sizeof(value));
  status = periph_flash_write_u16(flash, address, value);
  flash_demo_report_result(flash, "flash u16", status, address, expected, sizeof(expected));
}

static void flash_demo_test_u32(periph_flash_t *flash, uint32_t address)
{
  uint32_t value = 0x1234A5F0UL;
  uint8_t expected[sizeof(value)];
  osal_status_t status;

  memcpy(expected, &value, sizeof(value));
  status = periph_flash_write_u32(flash, address, value);
  flash_demo_report_result(flash, "flash u32", status, address, expected, sizeof(expected));
}

static void flash_demo_test_u64(periph_flash_t *flash, uint32_t address)
{
  uint64_t value = 0x1122334455667788ULL;
  uint8_t expected[sizeof(value)];
  osal_status_t status;

  memcpy(expected, &value, sizeof(value));
  status = periph_flash_write_u64(flash, address, value);
  flash_demo_report_result(flash, "flash u64", status, address, expected, sizeof(expected));
}

static void flash_demo_task(void *arg)
{
  app_flash_demo_ctx_t *ctx = (app_flash_demo_ctx_t *)arg;
  uint32_t base = OSAL_PLATFORM_FLASH_DEMO_ADDRESS;

  if ((ctx == NULL) || (ctx->flash == NULL) || ctx->done)
  {
    return;
  }

  ctx->done = true;

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

void app_flash_demo_init(void)
{
  osal_task_t *task;

  g_flash_demo_device = osal_platform_flash_create();
  if (g_flash_demo_device == NULL)
  {
    return;
  }

  g_flash_demo_ctx.flash = g_flash_demo_device;
  g_flash_demo_ctx.done = false;

  task = osal_task_create(flash_demo_task, &g_flash_demo_ctx, OSAL_TASK_PRIORITY_LOW);
  if (task != NULL)
  {
    (void)osal_task_start(task);
  }
}
#endif
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

  /* Initialize board peripherals generated by CubeMX. */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

  /*
   * OSAL bring-up sequence:
   * 1. osal_init() configures Cortex-M kernel peripherals used by OSAL:
   *    SysTick, NVIC Group (4) / SysTick priority, and optional DWT profiling.
   * 2. Each demo init below owns its own local state block and can be enabled independently.
   * 3. The cooperative scheduler starts in the main loop through osal_run().
   */
  osal_init();

  /* Console / basic visibility. */
#if OSAL_CFG_ENABLE_USART
  app_usart_demo_init();
#endif

  /* Core demos. Enable the ones you want to observe. */
  app_led_demo_init();
#if OSAL_CFG_ENABLE_QUEUE
  /* app_queue_demo_init(); */
#endif
#if OSAL_CFG_ENABLE_SW_TIMER
  /* app_timer_demo_init(); */
#endif
  /* app_rtt_demo_init(); */

#if OSAL_CFG_ENABLE_IRQ_PROFILE
  /* DWT profiling only reports system-layer internal critical sections. */
  app_dwt_profile_demo_init();
#endif

#if OSAL_CFG_ENABLE_FLASH && OSAL_PLATFORM_ENABLE_FLASH_DEMO
  /* Give the console a moment before the flash demo starts printing results. */
  HAL_Delay(1000U);
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
