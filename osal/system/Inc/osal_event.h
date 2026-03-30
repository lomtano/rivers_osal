#ifndef OSAL_EVENT_H
#define OSAL_EVENT_H

#include <stdbool.h>
#include <stdint.h>
#include "osal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct osal_event osal_event_t; /* 不透明事件句柄 */

/**
 * @brief 创建一个事件对象。
 * @param auto_reset 为 true 时，在等待成功后自动清除事件。
 * @return 成功返回事件句柄，失败返回 NULL。
 */
osal_event_t *osal_event_create(bool auto_reset);

/**
 * @brief 销毁一个事件对象。
 * @param evt 由 osal_event_create() 返回的事件句柄。
 */
void osal_event_delete(osal_event_t *evt);

/**
 * @brief 将事件置为已触发状态。
 * @param evt 事件句柄。
 * @return OSAL 状态码。
 */
osal_status_t osal_event_set(osal_event_t *evt);

/**
 * @brief 将事件清除为未触发状态。
 * @param evt 事件句柄。
 * @return OSAL 状态码。
 */
osal_status_t osal_event_clear(osal_event_t *evt);

/**
 * @brief 等待事件触发或超时。
 * @param evt 事件句柄。
 * @param timeout_ms 超时时间，单位为毫秒。
 * @return 成功返回 OSAL_OK，超时返回 OSAL_ERR_TIMEOUT。
 */
osal_status_t osal_event_wait(osal_event_t *evt, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_EVENT_H */
