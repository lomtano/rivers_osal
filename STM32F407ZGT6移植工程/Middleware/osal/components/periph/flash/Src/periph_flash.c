#include "osal.h"

#if OSAL_CFG_ENABLE_FLASH

#include "../Inc/periph_flash.h"
#include "osal_mem.h"
#include <string.h>

struct periph_flash {
    const periph_flash_bridge_t *bridge;
    void *context;
    struct periph_flash *next;
};

static periph_flash_t *s_flash_list = NULL;

/* 函数说明：输出 Flash 组件调试诊断信息。 */
static void periph_flash_report(const char *message) {
    OSAL_DEBUG_REPORT("flash", message);
}

/* 函数说明：将 Flash 对象挂入活动链表。 */
static void periph_flash_link(periph_flash_t *flash) {
    flash->next = s_flash_list;
    s_flash_list = flash;
}

/* 函数说明：检查 Flash 句柄是否仍在活动链表中。 */
static bool periph_flash_contains(periph_flash_t *flash) {
    periph_flash_t *current = s_flash_list;

    while (current != NULL) {
        if (current == flash) {
            return true;
        }
        current = current->next;
    }

    return false;
}

/* 函数说明：将 Flash 对象从活动链表中摘除。 */
static bool periph_flash_unlink(periph_flash_t *flash) {
    periph_flash_t *prev = NULL;
    periph_flash_t *current = s_flash_list;

    while (current != NULL) {
        if (current == flash) {
            if (prev == NULL) {
                s_flash_list = current->next;
            } else {
                prev->next = current->next;
            }
            current->next = NULL;
            return true;
        }
        prev = current;
        current = current->next;
    }

    return false;
}

/* 函数说明：校验 Flash 句柄是否有效。 */
static bool periph_flash_validate_handle(const periph_flash_t *flash) {
    if (flash == NULL) {
        return false;
    }
#if OSAL_CFG_ENABLE_DEBUG
    if (!periph_flash_contains((periph_flash_t *)flash)) {
        periph_flash_report("API called with inactive Flash handle");
        return false;
    }
#endif
    return true;
}

/* 函数说明：创建 Flash 桥接对象。 */
periph_flash_t *periph_flash_create(const periph_flash_bridge_t *bridge, void *context) {
    periph_flash_t *flash;

    if (osal_irq_is_in_isr()) {
        periph_flash_report("create is not allowed in ISR context");
        return NULL;
    }
    if (bridge == NULL) {
        periph_flash_report("create called with invalid bridge");
        return NULL;
    }

    flash = (periph_flash_t *)osal_mem_alloc((uint32_t)sizeof(periph_flash_t));
    if (flash == NULL) {
        return NULL;
    }

    flash->bridge = bridge;
    flash->context = context;
    flash->next = NULL;
    periph_flash_link(flash);
    return flash;
}

/* 函数说明：销毁 Flash 桥接对象。 */
void periph_flash_destroy(periph_flash_t *flash) {
    if (flash == NULL) {
        return;
    }
    if (osal_irq_is_in_isr()) {
        periph_flash_report("destroy is not allowed in ISR context");
        return;
    }
    if (!periph_flash_unlink(flash)) {
        periph_flash_report("destroy called with inactive Flash handle");
        return;
    }
    osal_mem_free(flash);
}

/* 函数说明：调用底层桥接解锁 Flash。 */
osal_status_t periph_flash_unlock(periph_flash_t *flash) {
    if (!periph_flash_validate_handle(flash)) {
        return OSAL_ERR_PARAM;
    }
    if ((flash->bridge == NULL) || (flash->bridge->unlock == NULL)) {
        return OSAL_ERR_PARAM;
    }
    return flash->bridge->unlock(flash->context);
}

/* 函数说明：调用底层桥接重新锁定 Flash。 */
osal_status_t periph_flash_lock(periph_flash_t *flash) {
    if (!periph_flash_validate_handle(flash)) {
        return OSAL_ERR_PARAM;
    }
    if ((flash->bridge == NULL) || (flash->bridge->lock == NULL)) {
        return OSAL_ERR_PARAM;
    }
    return flash->bridge->lock(flash->context);
}

/* 函数说明：擦除指定地址范围内的 Flash 空间。 */
osal_status_t periph_flash_erase(periph_flash_t *flash, uint32_t address, uint32_t length) {
    if ((!periph_flash_validate_handle(flash)) || (length == 0U)) {
        return OSAL_ERR_PARAM;
    }
    if ((flash->bridge == NULL) || (flash->bridge->erase == NULL)) {
        return OSAL_ERR_PARAM;
    }
    return flash->bridge->erase(flash->context, address, length);
}

/* 函数说明：从 Flash 读取一段数据到缓冲区。 */
osal_status_t periph_flash_read(periph_flash_t *flash, uint32_t address, uint8_t *data, uint32_t length) {
    if ((!periph_flash_validate_handle(flash)) || (data == NULL)) {
        return OSAL_ERR_PARAM;
    }

    if (length == 0U) {
        return OSAL_OK;
    }

    if ((flash->bridge != NULL) && (flash->bridge->read != NULL)) {
        return flash->bridge->read(flash->context, address, data, length);
    }

    memcpy(data, (const void *)(uintptr_t)address, length);
    return OSAL_OK;
}

/* 函数说明：按 8 位宽度写入一个 Flash 数据单元。 */
osal_status_t periph_flash_write_u8(periph_flash_t *flash, uint32_t address, uint8_t value) {
    if (!periph_flash_validate_handle(flash)) {
        return OSAL_ERR_PARAM;
    }
    if ((flash->bridge == NULL) || (flash->bridge->write_u8 == NULL)) {
        return OSAL_ERR_PARAM;
    }
    return flash->bridge->write_u8(flash->context, address, value);
}

/* 函数说明：按 16 位宽度写入一个 Flash 数据单元。 */
osal_status_t periph_flash_write_u16(periph_flash_t *flash, uint32_t address, uint16_t value) {
    if (!periph_flash_validate_handle(flash)) {
        return OSAL_ERR_PARAM;
    }
    if ((flash->bridge == NULL) || (flash->bridge->write_u16 == NULL)) {
        return OSAL_ERR_PARAM;
    }
    return flash->bridge->write_u16(flash->context, address, value);
}

/* 函数说明：按 32 位宽度写入一个 Flash 数据单元。 */
osal_status_t periph_flash_write_u32(periph_flash_t *flash, uint32_t address, uint32_t value) {
    if (!periph_flash_validate_handle(flash)) {
        return OSAL_ERR_PARAM;
    }
    if ((flash->bridge == NULL) || (flash->bridge->write_u32 == NULL)) {
        return OSAL_ERR_PARAM;
    }
    return flash->bridge->write_u32(flash->context, address, value);
}

/* 函数说明：按 64 位宽度写入一个 Flash 数据单元。 */
osal_status_t periph_flash_write_u64(periph_flash_t *flash, uint32_t address, uint64_t value) {
    if (!periph_flash_validate_handle(flash)) {
        return OSAL_ERR_PARAM;
    }
    if ((flash->bridge == NULL) || (flash->bridge->write_u64 == NULL)) {
        return OSAL_ERR_PARAM;
    }
    return flash->bridge->write_u64(flash->context, address, value);
}

static osal_status_t periph_flash_write_step(periph_flash_t *flash, uint32_t address, const uint8_t *data,
                                             uint32_t remaining, uint32_t *consumed) {
    uint64_t value64;
    uint32_t value32;
    uint16_t value16;

    if ((flash->bridge->write_u64 != NULL) && ((address & 0x7U) == 0U) && (remaining >= 8U)) {
        memcpy(&value64, data, sizeof(value64));
        *consumed = 8U;
        return flash->bridge->write_u64(flash->context, address, value64);
    }

    if ((flash->bridge->write_u32 != NULL) && ((address & 0x3U) == 0U) && (remaining >= 4U)) {
        memcpy(&value32, data, sizeof(value32));
        *consumed = 4U;
        return flash->bridge->write_u32(flash->context, address, value32);
    }

    if ((flash->bridge->write_u16 != NULL) && ((address & 0x1U) == 0U) && (remaining >= 2U)) {
        memcpy(&value16, data, sizeof(value16));
        *consumed = 2U;
        return flash->bridge->write_u16(flash->context, address, value16);
    }

    if (flash->bridge->write_u8 != NULL) {
        *consumed = 1U;
        return flash->bridge->write_u8(flash->context, address, data[0]);
    }

    *consumed = 0U;
    return OSAL_ERR_RESOURCE;
}

/* 函数说明：按桥接支持的写入宽度写入一段数据。 */
osal_status_t periph_flash_write(periph_flash_t *flash, uint32_t address, const uint8_t *data, uint32_t length) {
    osal_status_t status;
    uint32_t offset = 0U;

    if ((!periph_flash_validate_handle(flash)) || (data == NULL)) {
        return OSAL_ERR_PARAM;
    }
    if (flash->bridge == NULL) {
        return OSAL_ERR_PARAM;
    }

    while (offset < length) {
        uint32_t consumed = 0U;
        status = periph_flash_write_step(flash, address + offset, data + offset, length - offset, &consumed);
        if (status != OSAL_OK) {
            return status;
        }
        if (consumed == 0U) {
            return OSAL_ERR_RESOURCE;
        }
        offset += consumed;
    }

    return OSAL_OK;
}

#endif /* OSAL_CFG_ENABLE_FLASH */
