#include "osal_platform_stm32f4.h"
#include "osal.h"
#include "core_cm4.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_flash.h"
#include "stm32f4xx_hal_flash_ex.h"
#include "usart.h"
#include <string.h>

static TIM_HandleTypeDef s_tick_tim;
static periph_uart_t *s_uart_component = NULL;
static periph_flash_t *s_flash_component = NULL;
static bool s_tick_started = false;

/* 通过当前 UART 句柄发送单字节，给 USART 小组件复用。 */
static osal_status_t osal_platform_uart_write_byte(void *context, uint8_t byte) {
    UART_HandleTypeDef *uart = (UART_HandleTypeDef *)context;

    if (uart == NULL) {
        return OSAL_ERR_PARAM;
    }

    return (HAL_UART_Transmit(uart, &byte, 1U, 1000U) == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

/* 通过 STM32 HAL 解锁内部 Flash。 */
static osal_status_t osal_platform_flash_unlock(void *context) {
    (void)context;
    return (HAL_FLASH_Unlock() == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

/* 通过 STM32 HAL 锁定内部 Flash。 */
static osal_status_t osal_platform_flash_lock(void *context) {
    (void)context;
    return (HAL_FLASH_Lock() == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

/* 通过内存映射方式读取指定 Flash 字节区间。 */
static osal_status_t osal_platform_flash_read(void *context, uint32_t address, uint8_t *data, uint32_t length) {
    (void)context;

    if (data == NULL) {
        return OSAL_ERR_PARAM;
    }

    memcpy(data, (const void *)(uintptr_t)address, length);
    return OSAL_OK;
}

/* 将 STM32F4 的 Bank1 地址映射成扇区号。 */
static bool osal_platform_flash_sector_from_address(uint32_t address, uint32_t *sector) {
    if (sector == NULL) {
        return false;
    }

    if ((address < 0x08000000UL) || (address >= 0x08100000UL)) {
        return false;
    }

    if (address < 0x08004000UL) {
        *sector = FLASH_SECTOR_0;
    } else if (address < 0x08008000UL) {
        *sector = FLASH_SECTOR_1;
    } else if (address < 0x0800C000UL) {
        *sector = FLASH_SECTOR_2;
    } else if (address < 0x08010000UL) {
        *sector = FLASH_SECTOR_3;
    } else if (address < 0x08020000UL) {
        *sector = FLASH_SECTOR_4;
    } else if (address < 0x08040000UL) {
        *sector = FLASH_SECTOR_5;
    } else if (address < 0x08060000UL) {
        *sector = FLASH_SECTOR_6;
    } else if (address < 0x08080000UL) {
        *sector = FLASH_SECTOR_7;
    } else if (address < 0x080A0000UL) {
        *sector = FLASH_SECTOR_8;
    } else if (address < 0x080C0000UL) {
        *sector = FLASH_SECTOR_9;
    } else if (address < 0x080E0000UL) {
        *sector = FLASH_SECTOR_10;
    } else {
        *sector = FLASH_SECTOR_11;
    }

    return true;
}

/* 擦除目标地址范围所覆盖到的所有 STM32F4 扇区。 */
static osal_status_t osal_platform_flash_erase(void *context, uint32_t address, uint32_t length) {
    FLASH_EraseInitTypeDef erase_init;
    uint32_t first_sector;
    uint32_t last_sector;
    uint32_t sector_error = 0U;

    (void)context;

    if ((length == 0U) ||
        !osal_platform_flash_sector_from_address(address, &first_sector) ||
        !osal_platform_flash_sector_from_address(address + length - 1U, &last_sector)) {
        return OSAL_ERR_PARAM;
    }

    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase_init.Sector = first_sector;
    erase_init.NbSectors = (last_sector - first_sector) + 1U;

    return (HAL_FLASHEx_Erase(&erase_init, &sector_error) == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

/* 使用 STM32 HAL 按字节写入 Flash。 */
static osal_status_t osal_platform_flash_write_u8(void *context, uint32_t address, uint8_t value) {
    (void)context;
    return (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, address, value) == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

/* 使用 STM32 HAL 按半字写入 Flash。 */
static osal_status_t osal_platform_flash_write_u16(void *context, uint32_t address, uint16_t value) {
    (void)context;
    return (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address, value) == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

/* 使用 STM32 HAL 按字写入 Flash。 */
static osal_status_t osal_platform_flash_write_u32(void *context, uint32_t address, uint32_t value) {
    (void)context;
    return (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, value) == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

/* 使用 STM32 HAL 按双字写入 Flash。 */
static osal_status_t osal_platform_flash_write_u64(void *context, uint32_t address, uint64_t value) {
    (void)context;
    return (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, value) == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

static const periph_uart_bridge_t s_uart_bridge = {
    .write_byte = osal_platform_uart_write_byte
};

static const periph_flash_bridge_t s_flash_bridge = {
    .unlock = osal_platform_flash_unlock,
    .lock = osal_platform_flash_lock,
    .erase = osal_platform_flash_erase,
    .read = osal_platform_flash_read,
    .write_u8 = osal_platform_flash_write_u8,
    .write_u16 = osal_platform_flash_write_u16,
    .write_u32 = osal_platform_flash_write_u32,
    .write_u64 = osal_platform_flash_write_u64
};

/* 读取所选 TIMx 的输入时钟，用于计算 1MHz 预分频值。 */
static uint32_t osal_platform_tick_get_timer_clock_hz(void) {
    RCC_ClkInitTypeDef clk_config;
    uint32_t flash_latency;
    uint32_t pclk_hz;

    HAL_RCC_GetClockConfig(&clk_config, &flash_latency);

    if (OSAL_PLATFORM_TICK_TIM_APB_BUS == 2U) {
        pclk_hz = HAL_RCC_GetPCLK2Freq();
        if (clk_config.APB2CLKDivider != RCC_HCLK_DIV1) {
            pclk_hz *= 2U;
        }
    } else {
        pclk_hz = HAL_RCC_GetPCLK1Freq();
        if (clk_config.APB1CLKDivider != RCC_HCLK_DIV1) {
            pclk_hz *= 2U;
        }
    }

    return pclk_hz;
}

/* STM32F4 平台初始化钩子，保留给用户做板级准备。 */
void osal_platform_init(void) {
    /* HAL_Init(), clock setup, GPIO init, and USART init stay in user startup code. */
}

/* 创建或返回缓存的 STM32F4 USART 桥接组件。 */
periph_uart_t *osal_platform_uart_create(void) {
    if (s_uart_component == NULL) {
        s_uart_component = periph_uart_create(&s_uart_bridge, &huart2);
    }
    return s_uart_component;
}

/* 创建或返回缓存的 STM32F4 Flash 桥接组件。 */
periph_flash_t *osal_platform_flash_create(void) {
    if (s_flash_component == NULL) {
        s_flash_component = periph_flash_create(&s_flash_bridge, NULL);
    }
    return s_flash_component;
}

/* 启动所选 TIMx，让每次更新中断恰好对应 1us。 */
void osal_platform_tick_start(void) {
    uint32_t timer_clock_hz;
    uint32_t prescaler;

    if (s_tick_started) {
        return;
    }

    OSAL_PLATFORM_TICK_TIM_CLK_ENABLE();
    timer_clock_hz = osal_platform_tick_get_timer_clock_hz();
    if (timer_clock_hz < 1000000U) {
        return;
    }

    prescaler = (timer_clock_hz / 1000000U) - 1U;
    s_tick_tim.Instance = OSAL_PLATFORM_TICK_TIM_INSTANCE;
    s_tick_tim.Init.Prescaler = prescaler;
    s_tick_tim.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_tick_tim.Init.Period = 0U;
    s_tick_tim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_tick_tim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    (void)HAL_TIM_Base_Init(&s_tick_tim);

    __HAL_TIM_CLEAR_IT(&s_tick_tim, TIM_IT_UPDATE);
    __HAL_TIM_ENABLE_IT(&s_tick_tim, TIM_IT_UPDATE);
    HAL_NVIC_SetPriority(OSAL_PLATFORM_TICK_TIM_IRQn,
                         OSAL_PLATFORM_TICK_IRQ_PREEMPT_PRIO,
                         OSAL_PLATFORM_TICK_IRQ_SUBPRIO);
    HAL_NVIC_EnableIRQ(OSAL_PLATFORM_TICK_TIM_IRQn);
    (void)HAL_TIM_Base_Start(&s_tick_tim);
    s_tick_started = true;
}

/* 清除所选 TIMx 的更新中断标志。 */
void osal_platform_tick_ack(void) {
    __HAL_TIM_CLEAR_IT(&s_tick_tim, TIM_IT_UPDATE);
}

/* 通用 TIMx 模式下的中断处理骨架，只做清标志和累加 1us tick。 */
void osal_platform_tick_irq_handler(void) {
    if ((__HAL_TIM_GET_FLAG(&s_tick_tim, TIM_FLAG_UPDATE) != RESET) &&
        (__HAL_TIM_GET_IT_SOURCE(&s_tick_tim, TIM_IT_UPDATE) != RESET)) {
        osal_platform_tick_ack();
        osal_timer_inc_tick();
    }
}

/* SysTick 模式下的钩子骨架，若 SysTick 已经是 1us 周期，可直接调用。 */
void osal_platform_systick_handler(void) {
    osal_timer_inc_tick();
}

/* LED1 默认弱符号空实现，移植时由板级代码覆盖。 */
__weak void osal_platform_led1_toggle(void) {
}

/* LED2 默认弱符号空实现，移植时由板级代码覆盖。 */
__weak void osal_platform_led2_toggle(void) {
}

/* 判断当前是否运行在异常/中断上下文。 */
bool osal_irq_is_in_isr(void) {
    return (__get_IPSR() != 0U);
}

/* 关闭全局中断，并返回关闭前的 PRIMASK。 */
uint32_t osal_irq_disable(void) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

/* 显式打开全局中断。 */
void osal_irq_enable(void) {
    __enable_irq();
}

/* 按保存状态恢复全局中断。 */
void osal_irq_restore(uint32_t prev_state) {
    if (prev_state == 0U) {
        osal_irq_enable();
    }
}
