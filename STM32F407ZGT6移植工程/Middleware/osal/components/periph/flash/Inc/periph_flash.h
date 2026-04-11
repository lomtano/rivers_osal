#ifndef PERIPH_FLASH_H
#define PERIPH_FLASH_H

#include <stdint.h>
#include "osal.h"

#if !OSAL_CFG_ENABLE_FLASH
#error "OSAL flash component is disabled. Enable OSAL_CFG_ENABLE_FLASH in osal.h."
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct periph_flash periph_flash_t;

/* 桥接层按不同位宽分别暴露写接口，组件层不再替用户自动猜测该选哪种写入宽度。 */
typedef struct {
    /* context 由 create() 传入，很多片内 Flash 场景下可以为 NULL。 */
    osal_status_t (*unlock)(void *context);
    osal_status_t (*lock)(void *context);
    osal_status_t (*erase)(void *context, uint32_t address, uint32_t length);
    osal_status_t (*read)(void *context, uint32_t address, uint8_t *data, uint32_t length);
    osal_status_t (*write_u8)(void *context, uint32_t address, uint8_t value);
    osal_status_t (*write_u16)(void *context, uint32_t address, uint16_t value);
    osal_status_t (*write_u32)(void *context, uint32_t address, uint32_t value);
    osal_status_t (*write_u64)(void *context, uint32_t address, uint64_t value);
} periph_flash_bridge_t;

/*
 * Flash 组件句柄契约：
 * 1. create() 成功后，句柄所有权归调用方。
 * 2. destroy(NULL) 是安全空操作。
 * 3. destroy() 成功后，句柄立即失效，不能再次 unlock / erase / write / read / destroy。
 * 4. bridge 和 context 的生命周期由调用方保证，组件不会接管它们的所有权。
 * 5. debug 打开时，可检测到的重复 destroy、非法句柄会通过 OSAL_DEBUG_HOOK 报告。
 *
 * 接口能力矩阵：
 * - create / destroy: 任务态
 * - unlock / lock / erase / read / write_xxx: 默认按任务态使用
 * - 是否允许 ISR 使用，取决于底层 Flash SDK 与桥接实现，OSAL 不做默认保证
 */

/**
 * @brief 基于桥接函数表创建一个 Flash 组件实例。
 * @param bridge 目标 MCU SDK 对应的桥接回调表。
 * @param context 回传给桥接回调的用户上下文。
 * @return 成功时返回 Flash 组件句柄，失败时返回 NULL。
 * @note 组件不会替你判断“哪种位宽更合适”，位宽由调用哪个 write_uX 接口来决定。
 */
periph_flash_t *periph_flash_create(const periph_flash_bridge_t *bridge, void *context);

/**
 * @brief 销毁一个 Flash 组件实例。
 * @param flash Flash 组件句柄。
 */
void periph_flash_destroy(periph_flash_t *flash);

/**
 * @brief 解锁内部 Flash 编程。
 * @param flash Flash 组件句柄。
 * @return OSAL 状态码。
 */
osal_status_t periph_flash_unlock(periph_flash_t *flash);

/**
 * @brief 锁定内部 Flash 编程。
 * @param flash Flash 组件句柄。
 * @return OSAL 状态码。
 */
osal_status_t periph_flash_lock(periph_flash_t *flash);

/**
 * @brief 擦除一段 Flash 地址范围。
 * @param flash Flash 组件句柄。
 * @param address 起始地址。
 * @param length 擦除请求覆盖的字节数。
 * @return OSAL 状态码。
 * @note address/length 的对齐、扇区映射、页映射由具体桥接层自己处理。
 */
osal_status_t periph_flash_erase(periph_flash_t *flash, uint32_t address, uint32_t length);

/**
 * @brief 从 Flash 读取一段字节区间。
 * @param flash Flash 组件句柄。
 * @param address 读取起始地址。
 * @param data 目标缓冲区。
 * @param length 要读取的字节数。
 * @return OSAL 状态码。
 * @note 如果桥接层没提供 read 回调，某些平台实现会退回到“按内存映射直接读”。
 */
osal_status_t periph_flash_read(periph_flash_t *flash, uint32_t address, uint8_t *data, uint32_t length);

/**
 * @brief 向 Flash 写入一个字节。
 * @param flash Flash 组件句柄。
 * @param address 目标地址。
 * @param value 要写入的字节值。
 * @return OSAL 状态码。
 */
osal_status_t periph_flash_write_u8(periph_flash_t *flash, uint32_t address, uint8_t value);

/**
 * @brief 向 Flash 写入一个半字。
 * @param flash Flash 组件句柄。
 * @param address 目标地址。
 * @param value 要写入的半字值。
 * @return OSAL 状态码。
 */
osal_status_t periph_flash_write_u16(periph_flash_t *flash, uint32_t address, uint16_t value);

/**
 * @brief 向 Flash 写入一个字。
 * @param flash Flash 组件句柄。
 * @param address 目标地址。
 * @param value 要写入的字值。
 * @return OSAL 状态码。
 */
osal_status_t periph_flash_write_u32(periph_flash_t *flash, uint32_t address, uint32_t value);

/**
 * @brief 向 Flash 写入一个双字。
 * @param flash Flash 组件句柄。
 * @param address 目标地址。
 * @param value 要写入的双字值。
 * @return OSAL 状态码。
 */
osal_status_t periph_flash_write_u64(periph_flash_t *flash, uint32_t address, uint64_t value);

#ifdef __cplusplus
}
#endif

#endif /* PERIPH_FLASH_H */




