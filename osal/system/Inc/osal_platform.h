#ifndef OSAL_PLATFORM_H
#define OSAL_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t (*get_counter_clock_hz)(void);
    uint32_t (*get_reload_value)(void);
    uint32_t (*get_current_value)(void);
    bool (*is_enabled)(void);
    bool (*has_elapsed)(void);
} osal_tick_source_t;

/**
 * @brief 平台级初始化钩子。
 * @note 适配层只负责把 OSAL 和具体 MCU 连接起来，不建议在这里放复杂算法。
 */
void osal_platform_init(void);

/**
 * @brief 获取当前平台挂接给 OSAL 的 Tick 计数源。
 * @return 返回底层计数源读接口表；不使用硬件细分读数时可返回 NULL。
 */
const osal_tick_source_t *osal_platform_get_tick_source(void);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_PLATFORM_H */
