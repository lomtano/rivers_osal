#ifndef OSAL_H
#define OSAL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OSAL_OK = 0,
    OSAL_ERROR = 1,
    OSAL_ERR_TIMEOUT = 2,
    OSAL_ERR_RESOURCE = 3,
    OSAL_ERR_PARAM = 4,
    OSAL_ERR_NOMEM = 5,
    OSAL_ERR_ISR = 6,
    OSAL_RESERVED = 0x7FFFFFFF
} osal_status_t;

/**
 * @brief 初始化 OSAL 系统层。
 * @note 该接口会调用平台层初始化钩子，并自动同步当前 Tick 计数源配置。
 */
void osal_init(void);

/**
 * @brief 在周期性 Tick 中断里调用的 OSAL 通用中断入口。
 * @note 推荐直接在 SysTick_Handler() 或其他系统时基中断中调用它。
 */
void osal_tick_handler(void);

#include "osal_task.h"
#include "osal_queue.h"
#include "osal_mem.h"
#include "osal_irq.h"
#include "osal_event.h"
#include "osal_mutex.h"
#include "osal_timer.h"
#include "osal_platform.h"

#ifdef __cplusplus
}
#endif

#endif /* OSAL_H */
