#include "osal.h"

#if OSAL_CFG_ENABLE_FLASH

#include "../Inc/periph_flash.h"
#include "osal_mem.h"
#include <string.h>

/*
 * Flash 组件对象：
 * 1. bridge 指向底层 MCU SDK 的 Flash 操作函数表。
 * 2. context 保存底层上下文；对于很多片上 Flash，可能为空。
 * 3. next 仅用于活动对象链表管理。
 */
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
    /* 头插活动链表，后续 destroy/validate 都靠它检查句柄是否还活着。 */
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
        /* debug 模式下尽早发现“句柄已失效却继续使用”的问题。 */
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

    /* bridge 决定底层 MCU SDK 怎么操作 Flash，context 提供底层环境。 */
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
    /* 组件只拥有自己的控制块，不负责外部 SDK 句柄或板级资源的生命周期。 */
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
    /* 真正的解锁细节全部交给平台桥接层，例如 HAL_FLASH_Unlock。 */
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
    /* 组件层不直接碰厂商 SDK，只调桥接函数。 */
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
    /* 擦除按“地址 + 长度”描述，由板级桥接层自己处理扇区映射。 */
    return flash->bridge->erase(flash->context, address, length);
}

/* 函数说明：从 Flash 读取一段数据到缓冲区。 */
osal_status_t periph_flash_read(periph_flash_t *flash, uint32_t address, uint8_t *data, uint32_t length) {
    if ((!periph_flash_validate_handle(flash)) || (data == NULL)) {
        return OSAL_ERR_PARAM;
    }

    if (length == 0U) {
        /* 读 0 字节视为成功空操作，便于上层统一调用。 */
        return OSAL_OK;
    }

    if ((flash->bridge != NULL) && (flash->bridge->read != NULL)) {
        /* 如果平台单独提供了 read bridge，就优先走平台实现。 */
        return flash->bridge->read(flash->context, address, data, length);
    }

    /*
     * 如果平台没有单独提供 read bridge，则默认按“内部 Flash 可直接内存映射读取”处理。
     * 这适用于大多数片上 Flash，但不适用于某些特殊外部存储设备。
     */
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
    /* 这里不再替用户猜测最佳位宽，而是严格按显式接口名调用对应写入桥接。 */
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

#endif /* OSAL_CFG_ENABLE_FLASH */




