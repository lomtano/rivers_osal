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
#if OSAL_CFG_ENABLE_QUEUE
typedef struct {
  /* queue 当前是固定项大小的环形缓冲区，
   * 所以示例消息也设计成固定长度结构体。 */
  uint32_t sequence;
  uint8_t payload[8];
} app_queue_message_t;
#endif

/* -------------------------------------------------------------------------- */
/* Shared Tick Helper                                                         */
/* -------------------------------------------------------------------------- */

/*
 * 本文件里的大多数“周期示例”都采用同一种软件运行模式：
 *
 * 1. main() 里只负责创建任务、创建软件定时器，或者做一次性初始化。
 * 2. 主循环持续调用 osal_run()，由协作式调度器反复调用各个任务函数。
 * 3. 每个任务在自己被调度到时，只做一次“现在是不是到点了”的检查。
 * 4. 没到点就立刻 return，把执行机会让给其他任务；到点才真正做一次业务。
 * 5. 做完后把“下一次执行时间”往后推进，再等待下一轮调度。
 *
 * 这样写的根本原因不是为了模仿 RTOS delay，而是为了适配当前 OSAL 的真实模型：
 *
 * - 没有独立任务栈
 * - 没有阻塞等待后自动恢复
 * - 没有“从上次挂起那一行继续往下执行”的上下文语义
 *
 * 所以周期任务想非阻塞运行，就必须自己维护 deadline。
 */
static bool app_tick_reached(uint32_t now_ms, uint32_t deadline_ms)
{
  /* 这里不能直接写 now_ms >= deadline_ms。
   * 因为 32 位 tick 会自然回绕，差值比较才是跨回绕点后仍然安全的写法。 */
  return ((int32_t)(now_ms - deadline_ms) >= 0);
}

/* -------------------------------------------------------------------------- */
/* USART Demo                                                                 */
/* -------------------------------------------------------------------------- */

#if OSAL_CFG_ENABLE_USART
/* 绑定到示例串口后，printf/fputc 都会从这里走出去。 */
static periph_uart_t *g_usart_demo_uart = NULL;

int fputc(int ch, FILE *f)
{
  return periph_uart_fputc(ch, f);
}

void app_usart_demo_init(void)
{
  static const uint8_t raw_bytes[] = {'a', 'b', 'c', '\r', '\n'};

  /*
   * 软件运行模式：
   * 1. 这个示例不创建任务，它只在启动阶段执行一次。
   * 2. 先通过 platform 层创建一个 UART 设备对象。
   * 3. 再把 printf/fputc 绑定到这个 UART。
   * 4. 后续 main.c 里的其他串口日志都会复用这条输出链路。
   */
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

static void led_demo_task(void *arg)
{
  static bool s_led_deadline_initialized = false;
  static uint32_t s_led1_next_ms = 0U;
  static uint32_t s_led2_next_ms = 0U;
  uint32_t now_ms;

  (void)arg;
  /*
   * 软件运行模式：
   * 1. main() 只创建一个 LED 任务。
   * 2. 这个任务每轮被调度时都读取一次当前 tick。
   * 3. LED1 到 500ms 周期点就翻转一次，LED2 到 1000ms 周期点就翻转一次。
   * 4. 如果本轮两个周期点都没到，就立刻返回，不阻塞 CPU。
   *
   * 这里故意不用 delay，也不用 while 死等，
   * 就是为了展示当前 OSAL 推荐的“协作式非阻塞周期任务”写法。
   */

  now_ms = osal_timer_get_tick();
  if (!s_led_deadline_initialized)
  {
    /* 第一次运行时把两个 LED 的 deadline 都对齐到当前时刻，
     * 这样任务创建后第一轮调度就能立即看到一次翻转。 */
    s_led1_next_ms = now_ms;
    s_led2_next_ms = now_ms;
    s_led_deadline_initialized = true;
  }

  if (app_tick_reached(now_ms, s_led1_next_ms))
  {
    osal_platform_led1_toggle();
    do
    {
      /* 如果调度晚了不止一个周期，就一直往后追，
       * 避免后续节拍永久漂移。 */
      s_led1_next_ms += 500U;
    } while (app_tick_reached(now_ms, s_led1_next_ms));
  }

  if (app_tick_reached(now_ms, s_led2_next_ms))
  {
    osal_platform_led2_toggle();
    do
    {
      s_led2_next_ms += 1000U;
    } while (app_tick_reached(now_ms, s_led2_next_ms));
  }
}

void app_led_demo_init(void)
{
  osal_task_t *task;

  /* 这里只创建一个低优先级任务，让它在内部同时管理两个 LED 的节拍。 */
  task = osal_task_create(led_demo_task, NULL, OSAL_TASK_PRIORITY_LOW);
  if (task != NULL)
  {
    (void)osal_task_start(task);
  }
}

/* -------------------------------------------------------------------------- */
/* Queue Demo                                                                 */
/* -------------------------------------------------------------------------- */

#if OSAL_CFG_ENABLE_QUEUE
/* 这个示例里的 queue 只是一个固定项大小的环形缓冲区。 */
static osal_queue_t *g_queue_demo_queue = NULL;

static void queue_demo_producer_task(void *arg)
{
  static bool s_queue_send_deadline_initialized = false;
  static uint32_t s_queue_next_send_ms = 0U;
  static uint32_t s_queue_next_sequence = 0U;
  app_queue_message_t message;
  uint32_t now_ms;
  uint32_t i;
  osal_status_t status;

  (void)arg;
  /*
   * 软件运行模式：
   * 1. producer 任务每轮都只做一次“是否到发送周期”的判断。
   * 2. 到点后构造一条固定长度消息，并立即尝试一次非阻塞 send。
   * 3. send 成功才推进到下一条序号和下一次发送时间。
   * 4. 如果队列满了，就保持当前 deadline 已到期的状态，等待后续轮次继续尝试。
   *
   * 这正好体现当前 queue 的真实语义：
   * - 它是固定项大小的环形缓冲区
   * - 它没有等待链表
   * - 它不会把 producer/consumer 自动挂起再恢复
   */
  if (g_queue_demo_queue == NULL)
  {
    return;
  }

  now_ms = osal_timer_get_tick();
  if (!s_queue_send_deadline_initialized)
  {
    s_queue_next_send_ms = now_ms;
    s_queue_send_deadline_initialized = true;
  }

  if (!app_tick_reached(now_ms, s_queue_next_send_ms))
  {
    return;
  }

  message.sequence = s_queue_next_sequence;
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
    ++s_queue_next_sequence;
    do
    {
      s_queue_next_send_ms += 1000U;
    } while (app_tick_reached(now_ms, s_queue_next_send_ms));
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

  /*
   * 软件运行模式：
   * 1. consumer 任务没有自己的延时节拍。
   * 2. 它每轮被调度到时，都只做一次非阻塞 recv。
   * 3. 有消息就取走并打印，没有消息就立刻返回。
   *
   * 这是当前 queue 在协作式模型下最直接的使用方式：
   * 把“有没有消息”当成一次立即检查，而不是等待被 queue 唤醒。
   */
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

  /* 创建 8 个消息槽位，每个槽位都能完整放下一个 app_queue_message_t。 */
  g_queue_demo_queue = osal_queue_create(8U, (uint32_t)sizeof(app_queue_message_t));
  if (g_queue_demo_queue == NULL)
  {
    printf("queue create failed\r\n");
    return;
  }

  producer_task = osal_task_create(queue_demo_producer_task, NULL, OSAL_TASK_PRIORITY_HIGH);
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
/* 软件定时器回调最终在任务态执行，不会直接在 SysTick 中断里跑。
 * 这里示例改成直接走 printf，方便在串口控制台里观察输出。 */
static void oneshot_timer_callback(void *arg)
{
  (void)arg;
  printf("oneshot timer: %lu\r\n", (unsigned long)osal_timer_get_tick());
}

static void periodic_timer_callback(void *arg)
{
  (void)arg;
  printf("periodic timer: %lu\r\n", (unsigned long)osal_timer_get_tick());
}

void app_timer_demo_init(void)
{
  int oneshot_timer = osal_timer_create(2000000U, false, oneshot_timer_callback, NULL);
  int periodic_timer = osal_timer_create(1000000U, true, periodic_timer_callback, NULL);

  /*
   * 软件运行模式：
   * 1. 这里只创建两个软件定时器：一个 oneshot，一个 periodic。
   * 2. SysTick 中断本身只负责累计时间，不直接执行回调。
   * 3. 主循环里的 osal_run() 会调用 osal_timer_poll()。
   * 4. osal_timer_poll() 发现到期后，才在任务态执行下面的回调函数。
   *
   * 所以这个示例展示的是“软件定时器 + 串口输出”路径，
   * 不是“中断里直接打印”的路径。
   */
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

static void rtt_demo_task(void *arg)
{
  static bool s_rtt_initialized = false;
  static bool s_rtt_deadline_initialized = false;
  static uint32_t s_rtt_next_ms = 0U;
  uint32_t now_ms;

  (void)arg;
  /*
   * 软件运行模式：
   * 1. main() 创建一个中优先级 RTT 任务。
   * 2. 任务第一次运行时只做一次 SEGGER_RTT_Init()。
   * 3. 后续每 500ms 向 RTT 通道 0 打一条日志。
   * 4. 输出目的地是 J-Link RTT Viewer / RTT Client，不是串口。
   */
  if (!s_rtt_initialized)
  {
    /* RTT 控制块只需要初始化一次，后续每轮任务直接写日志即可。 */
    SEGGER_RTT_Init();
    s_rtt_initialized = true;
  }

  now_ms = osal_timer_get_tick();
  if (!s_rtt_deadline_initialized)
  {
    s_rtt_next_ms = now_ms;
    s_rtt_deadline_initialized = true;
  }

  if (!app_tick_reached(now_ms, s_rtt_next_ms))
  {
    return;
  }

  LOGW("rtt task running: %lu ms\r\n", (unsigned long)now_ms);
  do
  {
    s_rtt_next_ms += 500U;
  } while (app_tick_reached(now_ms, s_rtt_next_ms));
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
static void dwt_profile_demo_task(void *arg)
{
  static bool s_profile_deadline_initialized = false;
  static uint32_t s_profile_next_ms = 0U;
  osal_cortexm_profile_stats_t stats;
  uint32_t now_ms;
  uint32_t last_us;
  uint32_t min_us;
  uint32_t max_us;
  uint32_t avg_us;

  (void)arg;
  /*
   * 软件运行模式：
   * 1. DWT 是否真正启用，已经在 osal_init() 里由 cortexm 模块决定。
   * 2. system 层内部临界区会在后台持续累积 cycle 样本。
   * 3. 这个示例任务本身不做测量，只是每隔一段时间把当前统计结果读出来。
   * 4. 因此它更像“统计结果观察任务”，不是“临界区测量任务”。
   */
  if (!osal_cortexm_profile_get_stats(&stats))
  {
    /* 这里返回 false 通常表示：
     * 1. profiling 编译开关没打开
     * 2. 当前内核不支持 DWT CYCCNT */
    return;
  }

  now_ms = osal_timer_get_tick();
  if (!s_profile_deadline_initialized)
  {
    s_profile_next_ms = now_ms;
    s_profile_deadline_initialized = true;
  }

  if (!app_tick_reached(now_ms, s_profile_next_ms))
  {
    return;
  }

  last_us = osal_cortexm_profile_cycles_to_us(stats.last_cycles);
  min_us = osal_cortexm_profile_cycles_to_us(stats.min_cycles);
  max_us = osal_cortexm_profile_cycles_to_us(stats.max_cycles);
  avg_us = osal_cortexm_profile_cycles_to_us(stats.avg_cycles);
  /* 当前统计只覆盖 system 层内部显式包裹的临界区，
   * 不会把 main.c 里直接调用 osal_irq_* 的时间算进去。 */
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
  do
  {
    s_profile_next_ms += OSAL_CORTEXM_CRITICAL_PROFILE_PRINT_INTERVAL_MS;
  } while (app_tick_reached(now_ms, s_profile_next_ms));
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
static bool g_flash_demo_done = false;

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
    /* 写成功但回读校验失败时，把期望值和实际值都打出来，方便直接定位。 */
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
  uint32_t base = OSAL_PLATFORM_FLASH_DEMO_ADDRESS;

  (void)arg;
  /*
   * 软件运行模式：
   * 1. main() 里创建一个低优先级 flash 任务。
   * 2. 这个任务只在第一次真正执行时做一次完整测试。
   * 3. 流程固定为：unlock -> erase -> write -> read -> verify -> lock。
   * 4. 做完后把 done 置位，后续轮次立刻返回，避免重复擦写。
   */
  if ((g_flash_demo_device == NULL) || g_flash_demo_done)
  {
    return;
  }

  g_flash_demo_done = true;

  /* 这组写入地址故意错开，便于观察不同宽度写接口是否都按预期工作。 */
  printf("flash demo start @ 0x%08lX\r\n", (unsigned long)base);
  if (periph_flash_unlock(g_flash_demo_device) != OSAL_OK)
  {
    printf("flash unlock failed\r\n");
    return;
  }

  if (periph_flash_erase(g_flash_demo_device, base, 0x80U) != OSAL_OK)
  {
    printf("flash erase failed\r\n");
    (void)periph_flash_lock(g_flash_demo_device);
    return;
  }

  flash_demo_test_u8(g_flash_demo_device, base + 0x00U);
  flash_demo_test_u16(g_flash_demo_device, base + 0x10U);
  flash_demo_test_u32(g_flash_demo_device, base + 0x20U);
  flash_demo_test_u64(g_flash_demo_device, base + 0x30U);

  (void)periph_flash_lock(g_flash_demo_device);
}

void app_flash_demo_init(void)
{
  osal_task_t *task;

  g_flash_demo_device = osal_platform_flash_create();
  if (g_flash_demo_device == NULL)
  {
    return;
  }

  g_flash_demo_done = false;

  task = osal_task_create(flash_demo_task, NULL, OSAL_TASK_PRIORITY_LOW);
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
   * ========================= OSAL Bring-up =========================
   * 1. osal_init() configures Cortex-M kernel peripherals used by OSAL:
   *    SysTick, NVIC Group (4) / SysTick priority, and optional DWT profiling.
   * 2. Each demo init below owns its own local state block and can be enabled independently.
   * 3. The cooperative scheduler starts in the main loop through osal_run().
   */
  osal_init();

  /* ========================= Console Demo =========================
   * 先把控制台打通，后面其他示例的 printf 才有地方输出。 */
#if OSAL_CFG_ENABLE_USART
  app_usart_demo_init();
#endif

  /* =========================== Core Demos ===========================
   * 这里放最基础的 task / queue / timer / RTT 示例。
   * 哪个示例想观察就打开哪个 init；它们彼此独立，不要求全部同时启用。 */
  app_led_demo_init();
#if OSAL_CFG_ENABLE_QUEUE
  app_queue_demo_init();
#endif
#if OSAL_CFG_ENABLE_SW_TIMER
//  app_timer_demo_init();
#endif
  app_rtt_demo_init();

#if OSAL_CFG_ENABLE_IRQ_PROFILE
  /* ======================== DWT Profile Demo ========================
   * DWT profiling only reports system-layer internal critical sections. */
  app_dwt_profile_demo_init();
#endif

#if OSAL_CFG_ENABLE_FLASH && OSAL_PLATFORM_ENABLE_FLASH_DEMO
  /* ========================= Flash Demo =========================
   * flash 示例会连续打印擦写和回读结果，先给控制台一点启动时间。 */
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
