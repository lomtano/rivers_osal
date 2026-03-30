#include "osal_platform_cortexm.h"
#include "osal.h"

/*
 * 说明：
 * 1. 这是 Cortex-M 平台模板源文件。
 * 2. 这个文件主要用来告诉用户：哪些函数需要和 MCU SDK 对接。
 * 3. 当前工程真正参与编译的具体实例文件，是：
 *    platform/example/stm32f4/osal_platform_stm32f4.c
 * 4. 模板里的 Flash 桥接默认全部返回 OSAL_ERR_RESOURCE，
 *    你移植到新 MCU 时，需要按目标 SDK 把它们填完整。
 */

static periph_uart_t *s_uart_component = NULL;
static periph_flash_t *s_flash_component = NULL;

/* 串口桥接：把“发送单字节”能力接给 USART 小组件。 */
static osal_status_t osal_platform_uart_write_byte(void *context, uint8_t byte) {
    return (osal_status_t)OSAL_PLATFORM_UART_WRITE_BYTE(context, byte);
}

/*
 * Flash 桥接模板：
 * 如果目标 MCU 不支持某种写宽，可以保留为 OSAL_ERR_RESOURCE。
 */
static osal_status_t osal_platform_flash_unlock(void *context) {
    (void)context;
    return OSAL_ERR_RESOURCE;
}

static osal_status_t osal_platform_flash_lock(void *context) {
    (void)context;
    return OSAL_ERR_RESOURCE;
}

static osal_status_t osal_platform_flash_erase(void *context, uint32_t address, uint32_t length) {
    (void)context;
    (void)address;
    (void)length;
    return OSAL_ERR_RESOURCE;
}

static osal_status_t osal_platform_flash_read(void *context, uint32_t address, uint8_t *data, uint32_t length) {
    (void)context;
    (void)address;
    (void)data;
    (void)length;
    return OSAL_ERR_RESOURCE;
}

static osal_status_t osal_platform_flash_write_u8(void *context, uint32_t address, uint8_t value) {
    (void)context;
    (void)address;
    (void)value;
    return OSAL_ERR_RESOURCE;
}

static osal_status_t osal_platform_flash_write_u16(void *context, uint32_t address, uint16_t value) {
    (void)context;
    (void)address;
    (void)value;
    return OSAL_ERR_RESOURCE;
}

static osal_status_t osal_platform_flash_write_u32(void *context, uint32_t address, uint32_t value) {
    (void)context;
    (void)address;
    (void)value;
    return OSAL_ERR_RESOURCE;
}

static osal_status_t osal_platform_flash_write_u64(void *context, uint32_t address, uint64_t value) {
    (void)context;
    (void)address;
    (void)value;
    return OSAL_ERR_RESOURCE;
}

/* 系统时基桥接：这里只返回原始硬件读数，不做算法。 */
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
     * 模板里默认不做任何事。
     * 如果你想把某些 SDK 初始化动作集中到平台层，可以在具体实例文件里实现。
     */
}

const osal_tick_source_t *osal_platform_get_tick_source(void) {
    return &s_tick_source;
}

periph_uart_t *osal_platform_uart_create(void) {
    if (s_uart_component == NULL) {
        s_uart_component = periph_uart_create(&s_uart_bridge, (void *)OSAL_PLATFORM_UART_CONTEXT);
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

__weak bool osal_irq_is_in_isr(void) {
    return false;
}

__weak uint32_t osal_irq_disable(void) {
    return 0U;
}

__weak void osal_irq_enable(void) {
}

__weak void osal_irq_restore(uint32_t prev_state) {
    (void)prev_state;
}
