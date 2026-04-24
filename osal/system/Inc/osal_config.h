#ifndef OSAL_CONFIG_H
#define OSAL_CONFIG_H

/*
 * OSAL 统一配置入口。
 * 1. 所有功能开关统一放这里，不再散落在 osal.h 和各模块头文件里。
 * 2. 应用层如果要覆盖默认值，建议在包含 osal.h 之前先定义对应宏。
 */

#ifndef OSAL_CFG_ENABLE_DEBUG
#define OSAL_CFG_ENABLE_DEBUG 1
#endif

#ifndef OSAL_CFG_ENABLE_QUEUE
#define OSAL_CFG_ENABLE_QUEUE 1
#endif

#ifndef OSAL_CFG_ENABLE_IRQ_PROFILE
#define OSAL_CFG_ENABLE_IRQ_PROFILE 0
#endif

#ifndef OSAL_CFG_ENABLE_SW_TIMER
#define OSAL_CFG_ENABLE_SW_TIMER 1
#endif

#ifndef OSAL_CFG_ENABLE_USART
#define OSAL_CFG_ENABLE_USART 1
#endif

#ifndef OSAL_CFG_ENABLE_FLASH
#define OSAL_CFG_ENABLE_FLASH 1
#endif

#ifndef OSAL_CFG_INCLUDE_PLATFORM_HEADER
#define OSAL_CFG_INCLUDE_PLATFORM_HEADER 1
#endif

#ifndef OSAL_PLATFORM_HEADER_FILE
#define OSAL_PLATFORM_HEADER_FILE "osal_platform_stm32f4.h"
#endif

#endif /* OSAL_CONFIG_H */
