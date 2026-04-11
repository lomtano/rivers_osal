#include "osal_platform_stm32f4.h"
#include "osal.h"

#if OSAL_CFG_ENABLE_FLASH
#include "stm32f4xx_hal_flash.h"
#include "stm32f4xx_hal_flash_ex.h"
#include <string.h>
#endif

#if OSAL_CFG_ENABLE_USART
/* 当前板级示例只缓存一个默认 USART 组件实例，便于 main/integration 直接复用。 */
static periph_uart_t *s_uart_component = NULL;

/* 函数说明：调用当前平台 SDK 的单字节串口发送接口。 */
static osal_status_t osal_platform_uart_write_byte(void *context, uint8_t byte) {
    UART_HandleTypeDef *uart = (UART_HandleTypeDef *)context;

    if (uart == NULL) {
        return OSAL_ERR_PARAM;
    }

    /* 这里把 OSAL 的“发一个字节”语义直接映射到 STM32 HAL 的阻塞发送接口。 */
    return (HAL_UART_Transmit(uart, &byte, 1U, 1000U) == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

static const periph_uart_bridge_t s_uart_bridge = {
    .write_byte = osal_platform_uart_write_byte
};
#endif

#if OSAL_CFG_ENABLE_FLASH
/* 当前板级示例只缓存一个默认内部 Flash 组件实例。 */
static periph_flash_t *s_flash_component = NULL;

/* 函数说明：调用当前平台 SDK 解锁内部 Flash。 */
static osal_status_t osal_platform_flash_unlock(void *context) {
    (void)context;
    /* STM32 片内 Flash 是全局资源，通常不需要额外 context。 */
    return (HAL_FLASH_Unlock() == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

/* 函数说明：调用当前平台 SDK 锁定内部 Flash。 */
static osal_status_t osal_platform_flash_lock(void *context) {
    (void)context;
    return (HAL_FLASH_Lock() == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

/* 函数说明：按照当前平台地址空间读取一段 Flash 数据。 */
static osal_status_t osal_platform_flash_read(void *context, uint32_t address, uint8_t *data, uint32_t length) {
    (void)context;

    if (data == NULL) {
        return OSAL_ERR_PARAM;
    }

    memcpy(data, (const void *)(uintptr_t)address, length);
    /* STM32 片内 Flash 支持内存映射读取，因此这里不需要专门的 HAL 读函数。 */
    return OSAL_OK;
}

/*
 * 这里按 STM32F407 的片内 Flash 地址布局，把线性地址映射到 sector 编号。
 * 后续 erase() 会根据起始地址和结束地址分别求出首尾 sector，再批量擦除中间所有 sector。
 */
/* 函数说明：根据地址解析出所在 Flash 扇区编号。 */
static bool osal_platform_flash_sector_from_address(uint32_t address, uint32_t *sector) {
    if (sector == NULL) {
        return false;
    }

    if ((address < 0x08000000UL) || (address >= 0x08100000UL)) {
        /* 超出片内 Flash 地址范围时，直接判定为非法地址。 */
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

/* 函数说明：调用当前平台 SDK 擦除指定范围的 Flash 扇区。 */
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

    /* 根据起止地址计算首尾扇区，再让 HAL 一次性擦除整个 sector 区间。 */
    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase_init.Sector = first_sector;
    erase_init.NbSectors = (last_sector - first_sector) + 1U;

    /* HAL 会按首扇区和扇区数完成整段擦除。 */
    return (HAL_FLASHEx_Erase(&erase_init, &sector_error) == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

/* 函数说明：调用当前平台 SDK 以 8 位宽度写入 Flash。 */
static osal_status_t osal_platform_flash_write_u8(void *context, uint32_t address, uint8_t value) {
    (void)context;
    /* 这里严格对应 STM32 HAL 的 BYTE 编程模式。 */
    return (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, address, value) == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

/* 函数说明：调用当前平台 SDK 以 16 位宽度写入 Flash。 */
static osal_status_t osal_platform_flash_write_u16(void *context, uint32_t address, uint16_t value) {
    (void)context;
    /* 这里严格对应 STM32 HAL 的 HALFWORD 编程模式。 */
    return (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address, value) == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

/* 函数说明：调用当前平台 SDK 以 32 位宽度写入 Flash。 */
static osal_status_t osal_platform_flash_write_u32(void *context, uint32_t address, uint32_t value) {
    (void)context;
    /* 对 F407 这类芯片，32 位写通常是最常见、最稳的片内 Flash 写入方式。 */
    return (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, value) == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

/* 函数说明：调用当前平台 SDK 以 64 位宽度写入 Flash。 */
static osal_status_t osal_platform_flash_write_u64(void *context, uint32_t address, uint64_t value) {
    (void)context;
    /* 是否真支持 64 位写入，要以当前芯片/电压条件/SDK 返回结果为准。 */
    return (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, value) == HAL_OK) ? OSAL_OK : OSAL_ERROR;
}

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
#endif

/* 函数说明：完成当前平台所需的 OSAL 适配初始化。 */
void osal_platform_init(void) {
    /*
     * 当前工程里 HAL_Init()、时钟配置、GPIO 初始化、USART 初始化仍由 main.c 完成。
     * 如果你想把串口初始化集中到平台层，可以在这里主动调用 OSAL_PLATFORM_UART_INIT()。
     */
}

#if OSAL_CFG_ENABLE_USART
/* 函数说明：创建当前平台默认控制台 USART 桥接对象。 */
periph_uart_t *osal_platform_uart_create(void) {
    if (s_uart_component == NULL) {
        /* 只在第一次调用时真正创建组件，后面直接复用同一个实例。 */
        s_uart_component = periph_uart_create(&s_uart_bridge, &OSAL_PLATFORM_UART_HANDLE);
    }
    return s_uart_component;
}
#endif

#if OSAL_CFG_ENABLE_FLASH
/* 函数说明：创建当前平台默认内部 Flash 桥接对象。 */
periph_flash_t *osal_platform_flash_create(void) {
    if (s_flash_component == NULL) {
        /* 当前板级示例默认只暴露一个片内 Flash 组件实例。 */
        s_flash_component = periph_flash_create(&s_flash_bridge, NULL);
    }
    return s_flash_component;
}
#endif

/* 函数说明：翻转平台示例中的第一个 LED。 */
__weak void osal_platform_led1_toggle(void) {
    /* 用弱定义保留默认板级行为，用户可以在工程里自己写强定义覆盖它。 */
    OSAL_PLATFORM_LED1_TOGGLE();
}

/* 函数说明：翻转平台示例中的第二个 LED。 */
__weak void osal_platform_led2_toggle(void) {
    OSAL_PLATFORM_LED2_TOGGLE();
}




