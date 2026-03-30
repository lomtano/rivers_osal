#ifndef PERIPH_FLASH_H
#define PERIPH_FLASH_H

#include <stdint.h>
#include "osal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct periph_flash periph_flash_t;

typedef struct {
    osal_status_t (*unlock)(void *context);
    osal_status_t (*lock)(void *context);
    osal_status_t (*erase)(void *context, uint32_t address, uint32_t length);
    osal_status_t (*read)(void *context, uint32_t address, uint8_t *data, uint32_t length);
    osal_status_t (*write_u8)(void *context, uint32_t address, uint8_t value);
    osal_status_t (*write_u16)(void *context, uint32_t address, uint16_t value);
    osal_status_t (*write_u32)(void *context, uint32_t address, uint32_t value);
    osal_status_t (*write_u64)(void *context, uint32_t address, uint64_t value);
} periph_flash_bridge_t;

/**
 * @brief 基于桥接函数表创建一个 Flash 组件实例。
 * @param bridge 目标 MCU SDK 对应的桥接回调表。
 * @param context 回传给桥接回调的用户上下文。
 * @return 成功返回 Flash 组件句柄，失败返回 NULL。
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
 */
osal_status_t periph_flash_erase(periph_flash_t *flash, uint32_t address, uint32_t length);

/**
 * @brief 从 Flash 读取一段字节区间。
 * @param flash Flash 组件句柄。
 * @param address 读取起始地址。
 * @param data 目标缓冲区。
 * @param length 要读取的字节数。
 * @return OSAL 状态码。
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

/**
 * @brief 使用当前支持的最宽对齐写入方式写入一段字节缓冲区。
 * @param flash Flash 组件句柄。
 * @param address 目标地址。
 * @param data 源数据缓冲区。
 * @param length 要写入的字节数。
 * @return OSAL 状态码。
 */
osal_status_t periph_flash_write(periph_flash_t *flash, uint32_t address, const uint8_t *data, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif /* PERIPH_FLASH_H */
