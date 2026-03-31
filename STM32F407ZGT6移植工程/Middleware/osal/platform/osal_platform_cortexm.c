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

#if OSAL_CFG_ENABLE_USART
static periph_uart_t *s_uart_component = NULL;

/* 函数说明：调用当前平台 SDK 的单字节串口发送接口。 */
static osal_status_t osal_platform_uart_write_byte(void *context, uint8_t byte) {
    return (osal_status_t)OSAL_PLATFORM_UART_WRITE_BYTE(context, byte);
}

static const periph_uart_bridge_t s_uart_bridge = {
    .write_byte = osal_platform_uart_write_byte
};
#endif

#if OSAL_CFG_ENABLE_FLASH
static periph_flash_t *s_flash_component = NULL;

/* 函数说明：调用当前平台 SDK 解锁内部 Flash。 */
static osal_status_t osal_platform_flash_unlock(void *context) {
    (void)context;
    return OSAL_ERR_RESOURCE;
}

/* 函数说明：调用当前平台 SDK 锁定内部 Flash。 */
static osal_status_t osal_platform_flash_lock(void *context) {
    (void)context;
    return OSAL_ERR_RESOURCE;
}

/* 函数说明：调用当前平台 SDK 擦除指定范围的 Flash 扇区。 */
static osal_status_t osal_platform_flash_erase(void *context, uint32_t address, uint32_t length) {
    (void)context;
    (void)address;
    (void)length;
    return OSAL_ERR_RESOURCE;
}

/* 函数说明：按照当前平台地址空间读取一段 Flash 数据。 */
static osal_status_t osal_platform_flash_read(void *context, uint32_t address, uint8_t *data, uint32_t length) {
    (void)context;
    (void)address;
    (void)data;
    (void)length;
    return OSAL_ERR_RESOURCE;
}

/* 函数说明：调用当前平台 SDK 以 8 位宽度写入 Flash。 */
static osal_status_t osal_platform_flash_write_u8(void *context, uint32_t address, uint8_t value) {
    (void)context;
    (void)address;
    (void)value;
    return OSAL_ERR_RESOURCE;
}

/* 函数说明：调用当前平台 SDK 以 16 位宽度写入 Flash。 */
static osal_status_t osal_platform_flash_write_u16(void *context, uint32_t address, uint16_t value) {
    (void)context;
    (void)address;
    (void)value;
    return OSAL_ERR_RESOURCE;
}

/* 函数说明：调用当前平台 SDK 以 32 位宽度写入 Flash。 */
static osal_status_t osal_platform_flash_write_u32(void *context, uint32_t address, uint32_t value) {
    (void)context;
    (void)address;
    (void)value;
    return OSAL_ERR_RESOURCE;
}

/* 函数说明：调用当前平台 SDK 以 64 位宽度写入 Flash。 */
static osal_status_t osal_platform_flash_write_u64(void *context, uint32_t address, uint64_t value) {
    (void)context;
    (void)address;
    (void)value;
    return OSAL_ERR_RESOURCE;
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

/* 函数说明：读取当前系统时基源的输入时钟频率。 */
static uint32_t osal_platform_tick_source_get_clock_hz(void) {
    return OSAL_PLATFORM_TICK_SOURCE_CLOCK_HZ();
}

/* 函数说明：读取当前系统时基源的重装值。 */
static uint32_t osal_platform_tick_source_get_reload_value(void) {
    return OSAL_PLATFORM_TICK_SOURCE_RELOAD_VALUE();
}

/* 函数说明：读取当前系统时基源的当前计数值。 */
static uint32_t osal_platform_tick_source_get_current_value(void) {
    return OSAL_PLATFORM_TICK_SOURCE_CURRENT_VALUE();
}

/* 函数说明：判断当前系统时基源是否已经使能。 */
static bool osal_platform_tick_source_is_enabled(void) {
    return OSAL_PLATFORM_TICK_SOURCE_ENABLED();
}

/* 函数说明：判断当前系统时基源是否发生过一次计数回卷。 */
static bool osal_platform_tick_source_has_elapsed(void) {
    return OSAL_PLATFORM_TICK_SOURCE_ELAPSED();
}

static const osal_tick_source_t s_tick_source = {
    .get_counter_clock_hz = osal_platform_tick_source_get_clock_hz,
    .get_reload_value = osal_platform_tick_source_get_reload_value,
    .get_current_value = osal_platform_tick_source_get_current_value,
    .is_enabled = osal_platform_tick_source_is_enabled,
    .has_elapsed = osal_platform_tick_source_has_elapsed
};

/* 函数说明：完成当前平台所需的 OSAL 适配初始化。 */
void osal_platform_init(void) {
    /*
     * 模板里默认不做任何事。
     * 如果你想把某些 SDK 初始化动作集中到平台层，可以在具体实例文件里实现。
     */
}

/* 函数说明：返回当前平台注册的原始 Tick 源描述对象。 */
const osal_tick_source_t *osal_platform_get_tick_source(void) {
    return &s_tick_source;
}

#if OSAL_CFG_ENABLE_USART
/* 函数说明：创建当前平台默认控制台 USART 桥接对象。 */
periph_uart_t *osal_platform_uart_create(void) {
    if (s_uart_component == NULL) {
        s_uart_component = periph_uart_create(&s_uart_bridge, (void *)OSAL_PLATFORM_UART_CONTEXT);
    }
    return s_uart_component;
}
#endif

#if OSAL_CFG_ENABLE_FLASH
/* 函数说明：创建当前平台默认内部 Flash 桥接对象。 */
periph_flash_t *osal_platform_flash_create(void) {
    if (s_flash_component == NULL) {
        s_flash_component = periph_flash_create(&s_flash_bridge, NULL);
    }
    return s_flash_component;
}
#endif

/* 函数说明：翻转平台示例中的第一个 LED。 */
__weak void osal_platform_led1_toggle(void) {
    OSAL_PLATFORM_LED1_TOGGLE();
}

/* 函数说明：翻转平台示例中的第二个 LED。 */
__weak void osal_platform_led2_toggle(void) {
    OSAL_PLATFORM_LED2_TOGGLE();
}

/* 函数说明：判断当前代码是否运行在中断上下文中。 */
__weak bool osal_irq_is_in_isr(void) {
    return false;
}

/* 函数说明：关闭中断并返回当前中断状态快照。 */
__weak uint32_t osal_irq_disable(void) {
    return 0U;
}

/* 函数说明：重新打开全局中断。 */
__weak void osal_irq_enable(void) {
}

/* 函数说明：按之前保存的状态恢复中断开关。 */
__weak void osal_irq_restore(uint32_t prev_state) {
    (void)prev_state;
}
