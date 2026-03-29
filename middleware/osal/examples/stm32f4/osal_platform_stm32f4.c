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

/* Forward one byte through the active STM32 HAL UART handle. */
static osal_status_t osal_platform_uart_write_byte(void *context, uint8_t byte) {
    UART_HandleTypeDef *uart = (UART_HandleTypeDef *)context;

    if (uart == NULL) {
        return OSAL_ERR_PARAM;
    }

    return (HAL_UART_Transmit(uart, &byte, 1U, 1000U) == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

/* Unlock internal flash programming through the STM32 HAL. */
static osal_status_t osal_platform_flash_unlock(void *context) {
    (void)context;
    return (HAL_FLASH_Unlock() == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

/* Lock internal flash programming through the STM32 HAL. */
static osal_status_t osal_platform_flash_lock(void *context) {
    (void)context;
    return (HAL_FLASH_Lock() == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

/* Read back a raw flash byte range through memory-mapped access. */
static osal_status_t osal_platform_flash_read(void *context, uint32_t address, uint8_t *data, uint32_t length) {
    (void)context;

    if (data == NULL) {
        return OSAL_ERR_PARAM;
    }

    memcpy(data, (const void *)(uintptr_t)address, length);
    return OSAL_OK;
}

/* Map a bank-1 STM32F4 flash address into its sector index. */
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

/* Erase every STM32F4 sector touched by the requested byte range. */
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

/* Program one byte through the STM32 HAL flash API. */
static osal_status_t osal_platform_flash_write_u8(void *context, uint32_t address, uint8_t value) {
    (void)context;
    return (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, address, value) == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

/* Program one halfword through the STM32 HAL flash API. */
static osal_status_t osal_platform_flash_write_u16(void *context, uint32_t address, uint16_t value) {
    (void)context;
    return (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address, value) == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

/* Program one word through the STM32 HAL flash API. */
static osal_status_t osal_platform_flash_write_u32(void *context, uint32_t address, uint32_t value) {
    (void)context;
    return (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, value) == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

/* Program one doubleword through the STM32 HAL flash API. */
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

/* Example STM32F4 platform init hook kept separate from OSAL core logic. */
void osal_platform_init(void) {
    /* HAL_Init(), clock setup, GPIO init, and USART init stay in user startup code. */
}

/* Create or return the cached STM32F4 UART bridge component. */
periph_uart_t *osal_platform_uart_create(void) {
    if (s_uart_component == NULL) {
        s_uart_component = periph_uart_create(&s_uart_bridge, &huart2);
    }
    return s_uart_component;
}

/* Create or return the cached STM32F4 flash bridge component. */
periph_flash_t *osal_platform_flash_create(void) {
    if (s_flash_component == NULL) {
        s_flash_component = periph_flash_create(&s_flash_bridge, NULL);
    }
    return s_flash_component;
}

/* Start TIM2 so each update interrupt represents exactly 1 microsecond. */
void osal_platform_tick_start(void) {
    RCC_ClkInitTypeDef clk_config;
    uint32_t flash_latency;
    uint32_t timer_clock_hz;
    uint32_t prescaler;

    if (s_tick_started) {
        return;
    }

    __HAL_RCC_TIM2_CLK_ENABLE();
    HAL_RCC_GetClockConfig(&clk_config, &flash_latency);
    timer_clock_hz = HAL_RCC_GetPCLK1Freq();
    if (clk_config.APB1CLKDivider != RCC_HCLK_DIV1) {
        timer_clock_hz *= 2U;
    }

    prescaler = (timer_clock_hz / 1000000U) - 1U;
    s_tick_tim.Instance = TIM2;
    s_tick_tim.Init.Prescaler = prescaler;
    s_tick_tim.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_tick_tim.Init.Period = 0U;
    s_tick_tim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_tick_tim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    (void)HAL_TIM_Base_Init(&s_tick_tim);

    __HAL_TIM_CLEAR_IT(&s_tick_tim, TIM_IT_UPDATE);
    __HAL_TIM_ENABLE_IT(&s_tick_tim, TIM_IT_UPDATE);
    HAL_NVIC_SetPriority(TIM2_IRQn, 0U, 0U);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    (void)HAL_TIM_Base_Start(&s_tick_tim);
    s_tick_started = true;
}

/* Clear the TIM2 update interrupt flag used by the OSAL 1us tick. */
void osal_platform_tick_ack(void) {
    __HAL_TIM_CLEAR_IT(&s_tick_tim, TIM_IT_UPDATE);
}

/* Keep the TIM2 ISR lightweight by directly clearing the flag and bumping the OSAL tick. */
void osal_platform_tick_irq_handler(void) {
    if ((__HAL_TIM_GET_FLAG(&s_tick_tim, TIM_FLAG_UPDATE) != RESET) &&
        (__HAL_TIM_GET_IT_SOURCE(&s_tick_tim, TIM_IT_UPDATE) != RESET)) {
        osal_platform_tick_ack();
        osal_timer_inc_tick();
    }
}

/* Default LED1 hook for the STM32F4 example. Override with your board GPIO action. */
__weak void osal_platform_led1_toggle(void) {
}

/* Default LED2 hook for the STM32F4 example. Override with your board GPIO action. */
__weak void osal_platform_led2_toggle(void) {
}

/* Detect whether the current execution context is an exception handler. */
bool osal_irq_is_in_isr(void) {
    return (__get_IPSR() != 0U);
}

/* Disable global interrupts and return the previous PRIMASK state. */
uint32_t osal_irq_disable(void) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

/* Enable global interrupts directly. */
void osal_irq_enable(void) {
    __enable_irq();
}

/* Restore the global interrupt state saved by osal_irq_disable(). */
void osal_irq_restore(uint32_t prev_state) {
    if (prev_state == 0U) {
        osal_irq_enable();
    }
}
