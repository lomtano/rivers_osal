#ifndef PERIPH_FLASH_H
#define PERIPH_FLASH_H

#include <stdint.h>
#include "osal_status.h"

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
 * @brief Create a flash component instance from one bridge table.
 * @param bridge Bridge callbacks for the target MCU SDK.
 * @param context User context passed back into the bridge.
 * @return Flash component handle, or NULL on allocation failure.
 */
periph_flash_t *periph_flash_create(const periph_flash_bridge_t *bridge, void *context);

/**
 * @brief Destroy one flash component instance.
 * @param flash Flash component handle.
 */
void periph_flash_destroy(periph_flash_t *flash);

/**
 * @brief Unlock internal flash programming.
 * @param flash Flash component handle.
 * @return OSAL status code.
 */
osal_status_t periph_flash_unlock(periph_flash_t *flash);

/**
 * @brief Lock internal flash programming.
 * @param flash Flash component handle.
 * @return OSAL status code.
 */
osal_status_t periph_flash_lock(periph_flash_t *flash);

/**
 * @brief Erase one flash address range.
 * @param flash Flash component handle.
 * @param address Start address to erase.
 * @param length Number of bytes covered by the erase request.
 * @return OSAL status code.
 */
osal_status_t periph_flash_erase(periph_flash_t *flash, uint32_t address, uint32_t length);

/**
 * @brief Read a byte range from flash.
 * @param flash Flash component handle.
 * @param address Start address to read.
 * @param data Destination buffer.
 * @param length Number of bytes to read.
 * @return OSAL status code.
 */
osal_status_t periph_flash_read(periph_flash_t *flash, uint32_t address, uint8_t *data, uint32_t length);

/**
 * @brief Program one byte into flash.
 * @param flash Flash component handle.
 * @param address Destination address.
 * @param value Byte value to program.
 * @return OSAL status code.
 */
osal_status_t periph_flash_write_u8(periph_flash_t *flash, uint32_t address, uint8_t value);

/**
 * @brief Program one halfword into flash.
 * @param flash Flash component handle.
 * @param address Destination address.
 * @param value Halfword value to program.
 * @return OSAL status code.
 */
osal_status_t periph_flash_write_u16(periph_flash_t *flash, uint32_t address, uint16_t value);

/**
 * @brief Program one word into flash.
 * @param flash Flash component handle.
 * @param address Destination address.
 * @param value Word value to program.
 * @return OSAL status code.
 */
osal_status_t periph_flash_write_u32(periph_flash_t *flash, uint32_t address, uint32_t value);

/**
 * @brief Program one doubleword into flash.
 * @param flash Flash component handle.
 * @param address Destination address.
 * @param value Doubleword value to program.
 * @return OSAL status code.
 */
osal_status_t periph_flash_write_u64(periph_flash_t *flash, uint32_t address, uint64_t value);

/**
 * @brief Program a byte buffer using the widest supported aligned write callback.
 * @param flash Flash component handle.
 * @param address Destination address.
 * @param data Source buffer.
 * @param length Number of bytes to program.
 * @return OSAL status code.
 */
osal_status_t periph_flash_write(periph_flash_t *flash, uint32_t address, const uint8_t *data, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif // PERIPH_FLASH_H
