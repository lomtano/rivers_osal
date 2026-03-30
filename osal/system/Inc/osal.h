/******************************************************************************
 * Copyright (C) 2024-2026 rivers. All rights reserved.
 *
 * @author JH
 *
 * @version V1.0 2023-12-03
 *
 * @note 1 tab == 4 spaces!
 *
 *****************************************************************************/

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

#endif // OSAL_H
