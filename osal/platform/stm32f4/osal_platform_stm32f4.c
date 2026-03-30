#include "osal_platform_stm32f4.h"
#include "osal.h"
#include "stm32f4xx_hal_flash.h"
#include "stm32f4xx_hal_flash_ex.h"
#include <string.h>

static periph_uart_t *s_uart_component = NULL;
static periph_flash_t *s_flash_component = NULL;

/* 串口桥接：把 STM32 HAL 的单字节发送能力挂给 USART 小组件。 */
static osal_status_t osal_platform_uart_write_byte(void *context, uint8_t byte) {
    UART_HandleTypeDef *uart = (UART_HandleTypeDef *)context;

    if (uart == NULL) {
        return OSAL_ERR_PARAM;
    }

    return (HAL_UART_Transmit(uart, &byte, 1U, 1000U) == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

/* Flash 桥接：下面这一组函数负责把 STM32F4 的内部 Flash API 挂给组件层。 */
static osal_status_t osal_platform_flash_unlock(void *context) {
    /* 这里的 context 在当前示例中没有用到，所以显式丢弃它，避免编译器告警。 */
    (void)context;
    return (HAL_FLASH_Unlock() == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

static osal_status_t osal_platform_flash_lock(void *context) {
    (void)context;
    return (HAL_FLASH_Lock() == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

static osal_status_t osal_platform_flash_read(void *context, uint32_t address, uint8_t *data, uint32_t length) {
    (void)context;

    if (data == NULL) {
        return OSAL_ERR_PARAM;
    }

    memcpy(data, (const void *)(uintptr_t)address, length);
    return OSAL_OK;
}

/* 当前地址到扇区号的映射只适用于 STM32F407 Bank1。换型号时通常需要改这里。 */
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

static osal_status_t osal_platform_flash_write_u8(void *context, uint32_t address, uint8_t value) {
    (void)context;
    return (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, address, value) == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

static osal_status_t osal_platform_flash_write_u16(void *context, uint32_t address, uint16_t value) {
    (void)context;
    return (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address, value) == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

static osal_status_t osal_platform_flash_write_u32(void *context, uint32_t address, uint32_t value) {
    (void)context;
    return (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, value) == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

static osal_status_t osal_platform_flash_write_u64(void *context, uint32_t address, uint64_t value) {
    (void)context;
    return (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, value) == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

/* SysTick 原始读桥接：这里只返回硬件原始值，不做系统层算法。 */
static uint32_t osal_platform_tick_source_get_clock_hz(void) {
    return OSAL_PLATFORM_TICK_SOURCE_CLOCK_HZ();
}

static uint32_t osal_platform_tick_source_get_reload_value(void) {
    return OSAL_PLATFORM_TICK_SOURCE_RELOAD_VALUE();
}

static uint32_t osal_platform_tick_source_get_current_value(void) {
    return OSAL_PLATFORM_TICK_SOURCE_CURRENT_VALUE();
}

static bool osal_platform_tick_source_is_enabled(void) {
    return OSAL_PLATFORM_TICK_SOURCE_ENABLED();
}

static bool osal_platform_tick_source_has_elapsed(void) {
    return OSAL_PLATFORM_TICK_SOURCE_ELAPSED();
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

static const osal_tick_source_t s_tick_source = {
    .get_counter_clock_hz = osal_platform_tick_source_get_clock_hz,
    .get_reload_value = osal_platform_tick_source_get_reload_value,
    .get_current_value = osal_platform_tick_source_get_current_value,
    .is_enabled = osal_platform_tick_source_is_enabled,
    .has_elapsed = osal_platform_tick_source_has_elapsed
};

void osal_platform_init(void) {
    /*
     * 当前工程里 HAL_Init()、时钟配置、GPIO 初始化、USART 初始化仍由 main.c 完成。
     * 如果你以后想把串口初始化也下沉到平台层，可以在这里主动调用：
     * OSAL_PLATFORM_UART_INIT();
     */
}

const osal_tick_source_t *osal_platform_get_tick_source(void) {
    return &s_tick_source;
}

periph_uart_t *osal_platform_uart_create(void) {
    if (s_uart_component == NULL) {
        s_uart_component = periph_uart_create(&s_uart_bridge, &OSAL_PLATFORM_UART_HANDLE);
    }
    return s_uart_component;
}

periph_flash_t *osal_platform_flash_create(void) {
    if (s_flash_component == NULL) {
        s_flash_component = periph_flash_create(&s_flash_bridge, NULL);
    }
    return s_flash_component;
}

__weak void osal_platform_led1_toggle(void) {
    OSAL_PLATFORM_LED1_TOGGLE();
}

__weak void osal_platform_led2_toggle(void) {
    OSAL_PLATFORM_LED2_TOGGLE();
}

bool osal_irq_is_in_isr(void) {
    return (__get_IPSR() != 0U);
}

uint32_t osal_irq_disable(void) {
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    return primask;
}

void osal_irq_enable(void) {
    __enable_irq();
}

void osal_irq_restore(uint32_t prev_state) {
    if (prev_state == 0U) {
        osal_irq_enable();
    }
}
