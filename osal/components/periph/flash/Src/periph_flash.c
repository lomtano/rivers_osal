#include "../Inc/periph_flash.h"
#include "osal_mem.h"
#include <string.h>

struct periph_flash {
    const periph_flash_bridge_t *bridge;
    void *context;
};

periph_flash_t *periph_flash_create(const periph_flash_bridge_t *bridge, void *context) {
    periph_flash_t *flash;

    if (bridge == NULL) {
        return NULL;
    }

    flash = (periph_flash_t *)osal_mem_alloc((uint32_t)sizeof(periph_flash_t));
    if (flash == NULL) {
        return NULL;
    }

    flash->bridge = bridge;
    flash->context = context;
    return flash;
}

void periph_flash_destroy(periph_flash_t *flash) {
    if (flash == NULL) {
        return;
    }
    osal_mem_free(flash);
}

osal_status_t periph_flash_unlock(periph_flash_t *flash) {
    if (flash == NULL || flash->bridge == NULL || flash->bridge->unlock == NULL) {
        return OSAL_ERR_PARAM;
    }
    return flash->bridge->unlock(flash->context);
}

osal_status_t periph_flash_lock(periph_flash_t *flash) {
    if (flash == NULL || flash->bridge == NULL || flash->bridge->lock == NULL) {
        return OSAL_ERR_PARAM;
    }
    return flash->bridge->lock(flash->context);
}

osal_status_t periph_flash_erase(periph_flash_t *flash, uint32_t address, uint32_t length) {
    if (flash == NULL || flash->bridge == NULL || flash->bridge->erase == NULL || length == 0U) {
        return OSAL_ERR_PARAM;
    }
    return flash->bridge->erase(flash->context, address, length);
}

osal_status_t periph_flash_read(periph_flash_t *flash, uint32_t address, uint8_t *data, uint32_t length) {
    if (flash == NULL || data == NULL) {
        return OSAL_ERR_PARAM;
    }

    if (length == 0U) {
        return OSAL_OK;
    }

    if (flash->bridge != NULL && flash->bridge->read != NULL) {
        return flash->bridge->read(flash->context, address, data, length);
    }

    memcpy(data, (const void *)(uintptr_t)address, length);
    return OSAL_OK;
}

osal_status_t periph_flash_write_u8(periph_flash_t *flash, uint32_t address, uint8_t value) {
    if (flash == NULL || flash->bridge == NULL || flash->bridge->write_u8 == NULL) {
        return OSAL_ERR_PARAM;
    }
    return flash->bridge->write_u8(flash->context, address, value);
}

osal_status_t periph_flash_write_u16(periph_flash_t *flash, uint32_t address, uint16_t value) {
    if (flash == NULL || flash->bridge == NULL || flash->bridge->write_u16 == NULL) {
        return OSAL_ERR_PARAM;
    }
    return flash->bridge->write_u16(flash->context, address, value);
}

osal_status_t periph_flash_write_u32(periph_flash_t *flash, uint32_t address, uint32_t value) {
    if (flash == NULL || flash->bridge == NULL || flash->bridge->write_u32 == NULL) {
        return OSAL_ERR_PARAM;
    }
    return flash->bridge->write_u32(flash->context, address, value);
}

osal_status_t periph_flash_write_u64(periph_flash_t *flash, uint32_t address, uint64_t value) {
    if (flash == NULL || flash->bridge == NULL || flash->bridge->write_u64 == NULL) {
        return OSAL_ERR_PARAM;
    }
    return flash->bridge->write_u64(flash->context, address, value);
}

static osal_status_t periph_flash_write_step(periph_flash_t *flash, uint32_t address, const uint8_t *data, uint32_t remaining, uint32_t *consumed) {
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

osal_status_t periph_flash_write(periph_flash_t *flash, uint32_t address, const uint8_t *data, uint32_t length) {
    osal_status_t status;
    uint32_t offset = 0U;

    if (flash == NULL || flash->bridge == NULL || data == NULL) {
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
