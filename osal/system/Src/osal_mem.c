#include "../Inc/osal_mem.h"
#include "../Inc/osal_irq.h"
#include <stdbool.h>
#include <string.h>

#define OSAL_MEM_ALIGN           ((uint32_t)sizeof(void *))
#define OSAL_BLOCK_USED_FLAG     (0x80000000UL)
#define OSAL_BLOCK_SIZE_MASK     (0x7FFFFFFFUL)

typedef struct osal_heap_block {
    uint32_t size_and_flags;
    struct osal_heap_block *next_free;
} osal_heap_block_t;

struct osal_mempool {
    uint8_t *buf;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t stride;
    void *free_list;
    struct osal_mempool *next;
};

static uint8_t *s_heap_buffer = NULL;
static uint32_t s_heap_size = 0U;
static osal_heap_block_t *s_free_list = NULL;
static bool s_heap_ready = false;
static osal_mempool_t *s_mempool_list = NULL;

typedef union {
    void *align;
    uint8_t bytes[OSAL_HEAP_SIZE];
} osal_default_heap_t;

static osal_default_heap_t s_default_heap;

/* 函数说明：输出内存模块调试诊断信息。 */
static void osal_mem_report(const char *message) {
    OSAL_DEBUG_REPORT("mem", message);
}

/* 函数说明：将字节数向上对齐到系统对齐粒度。 */
static uint32_t osal_mem_align_up(uint32_t value) {
    uint32_t mask = OSAL_MEM_ALIGN - 1U;
    return (value + mask) & ~mask;
}

/* 函数说明：将字节数向下对齐到系统对齐粒度。 */
static uint32_t osal_mem_align_down(uint32_t value) {
    uint32_t mask = OSAL_MEM_ALIGN - 1U;
    return value & ~mask;
}

/* 函数说明：读取堆块当前记录的有效大小。 */
static uint32_t osal_heap_block_size(const osal_heap_block_t *block) {
    return (block->size_and_flags & OSAL_BLOCK_SIZE_MASK);
}

/* 函数说明：检查堆块是否处于已分配状态。 */
static bool osal_heap_block_used(const osal_heap_block_t *block) {
    return ((block->size_and_flags & OSAL_BLOCK_USED_FLAG) != 0U);
}

/* 函数说明：更新堆块头中的大小和使用标志。 */
static void osal_heap_set_block(osal_heap_block_t *block, uint32_t size, bool used) {
    block->size_and_flags = (size & OSAL_BLOCK_SIZE_MASK) | (used ? OSAL_BLOCK_USED_FLAG : 0U);
}

/* 函数说明：获取固定块内下一指针字段的地址。 */
static void **osal_block_next_ptr(void *block) {
    return (void **)block;
}

/* 函数说明：确保统一 OSAL 堆已经完成初始化。 */
static void osal_mem_ensure_init(void) {
    if (!s_heap_ready) {
        osal_mem_init(NULL, 0U);
    }
}

/* 函数说明：检查指针是否落在 OSAL 堆地址范围内。 */
static bool osal_mem_pointer_in_heap(const void *ptr) {
    const uint8_t *byte_ptr = (const uint8_t *)ptr;

    if ((!s_heap_ready) || (s_heap_buffer == NULL) || (s_heap_size == 0U) || (ptr == NULL)) {
        return false;
    }

    return ((byte_ptr >= s_heap_buffer) && (byte_ptr < (s_heap_buffer + s_heap_size)));
}

/* 函数说明：将内存池对象挂入活动链表。 */
static void osal_mempool_link(osal_mempool_t *mp) {
    mp->next = s_mempool_list;
    s_mempool_list = mp;
}

/* 函数说明：检查内存池句柄是否仍在活动链表中。 */
static bool osal_mempool_contains(osal_mempool_t *mp) {
    osal_mempool_t *current = s_mempool_list;

    while (current != NULL) {
        if (current == mp) {
            return true;
        }
        current = current->next;
    }

    return false;
}

/* 函数说明：将内存池对象从活动链表中摘除。 */
static bool osal_mempool_unlink(osal_mempool_t *mp) {
    osal_mempool_t *prev = NULL;
    osal_mempool_t *current = s_mempool_list;

    while (current != NULL) {
        if (current == mp) {
            if (prev == NULL) {
                s_mempool_list = current->next;
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

/* 函数说明：校验内存池句柄是否有效。 */
static bool osal_mempool_validate_handle(const osal_mempool_t *mp) {
    if (mp == NULL) {
        return false;
    }
#if OSAL_CFG_ENABLE_DEBUG
    if (!osal_mempool_contains((osal_mempool_t *)mp)) {
        osal_mem_report("mempool API called with inactive mempool handle");
        return false;
    }
#endif
    return true;
}

/* 函数说明：初始化统一 OSAL 堆区域。 */
void osal_mem_init(void *heap_buffer, uint32_t heap_size) {
    uint8_t *buffer;
    uint32_t size;
    uint32_t aligned_size;
    osal_heap_block_t *initial;

    if (heap_buffer != NULL && heap_size != 0U) {
        buffer = (uint8_t *)heap_buffer;
        size = heap_size;
    } else {
        buffer = s_default_heap.bytes;
        size = (uint32_t)sizeof(s_default_heap.bytes);
    }

    aligned_size = osal_mem_align_down(size);
    if (aligned_size <= sizeof(osal_heap_block_t)) {
        s_heap_buffer = NULL;
        s_heap_size = 0U;
        s_free_list = NULL;
        s_heap_ready = false;
        return;
    }

    s_heap_buffer = buffer;
    s_heap_size = aligned_size;
    s_free_list = (osal_heap_block_t *)s_heap_buffer;
    initial = s_free_list;
    osal_heap_set_block(initial, s_heap_size, false);
    initial->next_free = NULL;
    s_heap_ready = true;
}

/* 函数说明：从统一 OSAL 堆中分配一段内存。 */
void *osal_mem_alloc(uint32_t size) {
    uint32_t irq_state;
    uint32_t request_size;
    osal_heap_block_t *prev;
    osal_heap_block_t *current;

    if (size == 0U) {
        return NULL;
    }

    osal_mem_ensure_init();
    if (!s_heap_ready) {
        return NULL;
    }

    request_size = osal_mem_align_up(size + (uint32_t)sizeof(osal_heap_block_t));

    irq_state = osal_irq_disable();
    prev = NULL;
    current = s_free_list;

    while (current != NULL) {
        uint32_t current_size = osal_heap_block_size(current);
        if (current_size >= request_size) {
            uint32_t remain = current_size - request_size;
            if (remain > (uint32_t)sizeof(osal_heap_block_t)) {
                osal_heap_block_t *next = (osal_heap_block_t *)((uint8_t *)current + request_size);
                osal_heap_set_block(next, remain, false);
                next->next_free = current->next_free;
                if (prev == NULL) {
                    s_free_list = next;
                } else {
                    prev->next_free = next;
                }
                osal_heap_set_block(current, request_size, true);
            } else {
                if (prev == NULL) {
                    s_free_list = current->next_free;
                } else {
                    prev->next_free = current->next_free;
                }
                osal_heap_set_block(current, current_size, true);
            }
            current->next_free = NULL;
            osal_irq_restore(irq_state);
            return (uint8_t *)current + sizeof(osal_heap_block_t);
        }
        prev = current;
        current = current->next_free;
    }

    osal_irq_restore(irq_state);
    return NULL;
}

/* 函数说明：按地址顺序插入一个空闲堆块并尝试合并。 */
static void osal_mem_insert_free_block(osal_heap_block_t *block) {
    osal_heap_block_t *prev = NULL;
    osal_heap_block_t *current = s_free_list;

    while (current != NULL && current < block) {
        prev = current;
        current = current->next_free;
    }

    block->next_free = current;
    if (prev == NULL) {
        s_free_list = block;
    } else {
        prev->next_free = block;
    }

    if (block->next_free != NULL) {
        uint8_t *block_end = (uint8_t *)block + osal_heap_block_size(block);
        if (block_end == (uint8_t *)block->next_free) {
            osal_heap_set_block(block, osal_heap_block_size(block) + osal_heap_block_size(block->next_free), false);
            block->next_free = block->next_free->next_free;
        }
    }

    if (prev != NULL) {
        uint8_t *prev_end = (uint8_t *)prev + osal_heap_block_size(prev);
        if (prev_end == (uint8_t *)block) {
            osal_heap_set_block(prev, osal_heap_block_size(prev) + osal_heap_block_size(block), false);
            prev->next_free = block->next_free;
        }
    }
}

/* 函数说明：将一段堆内存归还到统一 OSAL 堆。 */
void osal_mem_free(void *ptr) {
    uint32_t irq_state;
    osal_heap_block_t *block;
    uint8_t *user_ptr;

    if (ptr == NULL) {
        return;
    }

    osal_mem_ensure_init();
    if (!s_heap_ready) {
        return;
    }

    user_ptr = (uint8_t *)ptr;
    if (!osal_mem_pointer_in_heap(user_ptr) || (user_ptr < (s_heap_buffer + sizeof(osal_heap_block_t)))) {
        osal_mem_report("free called with pointer outside OSAL heap");
        return;
    }

    block = (osal_heap_block_t *)(user_ptr - sizeof(osal_heap_block_t));
    if (!osal_mem_pointer_in_heap(block)) {
        osal_mem_report("free called with invalid heap block header");
        return;
    }
    if (!osal_heap_block_used(block)) {
        osal_mem_report("double free or inactive heap block detected");
        return;
    }

    irq_state = osal_irq_disable();
    osal_heap_set_block(block, osal_heap_block_size(block), false);
    osal_mem_insert_free_block(block);
    osal_irq_restore(irq_state);
}

/* 函数说明：获取统一 OSAL 堆当前剩余的可用字节数。 */
uint32_t osal_mem_get_free_size(void) {
    uint32_t irq_state;
    uint32_t total = 0U;
    osal_heap_block_t *current;

    osal_mem_ensure_init();
    if (!s_heap_ready) {
        return 0U;
    }

    irq_state = osal_irq_disable();
    current = s_free_list;
    while (current != NULL) {
        total += osal_heap_block_size(current);
        current = current->next_free;
    }
    osal_irq_restore(irq_state);

    return total;
}

/* 函数说明：根据用户提供缓冲区创建一个固定块内存池。 */
osal_mempool_t *osal_mempool_create(void *pool_buffer, uint32_t block_size, uint32_t block_count) {
    osal_mempool_t *mp;
    uint32_t block;

    if (osal_irq_is_in_isr()) {
        osal_mem_report("mempool_create is not allowed in ISR context");
        return NULL;
    }
    if ((pool_buffer == NULL) || (block_size == 0U) || (block_count == 0U)) {
        return NULL;
    }

    mp = (osal_mempool_t *)osal_mem_alloc((uint32_t)sizeof(osal_mempool_t));
    if (mp == NULL) {
        return NULL;
    }

    mp->buf = (uint8_t *)pool_buffer;
    mp->block_size = block_size;
    mp->block_count = block_count;
    mp->stride = (block_size < (uint32_t)sizeof(void *)) ? (uint32_t)sizeof(void *) : osal_mem_align_up(block_size);
    mp->free_list = pool_buffer;
    mp->next = NULL;

    for (block = 0U; block < block_count; ++block) {
        uint8_t *current = mp->buf + (block * mp->stride);
        uint8_t *next = (block + 1U < block_count) ? (mp->buf + ((block + 1U) * mp->stride)) : NULL;
        *osal_block_next_ptr(current) = next;
    }

    osal_mempool_link(mp);
    return mp;
}

/* 函数说明：删除一个固定块内存池对象。 */
void osal_mempool_delete(osal_mempool_t *mp) {
    if (mp == NULL) {
        return;
    }
    if (osal_irq_is_in_isr()) {
        osal_mem_report("mempool_delete is not allowed in ISR context");
        return;
    }
    if (!osal_mempool_unlink(mp)) {
        osal_mem_report("mempool_delete called with inactive mempool handle");
        return;
    }
    osal_mem_free(mp);
}

/* 函数说明：从固定块内存池中申请一个块。 */
void *osal_mempool_alloc(osal_mempool_t *mp) {
    void *block;
    uint32_t irq_state;

    if (!osal_mempool_validate_handle(mp)) {
        return NULL;
    }
    if (mp->free_list == NULL) {
        return NULL;
    }

    irq_state = osal_irq_disable();
    block = mp->free_list;
    mp->free_list = *osal_block_next_ptr(block);
    osal_irq_restore(irq_state);
    return block;
}

/* 函数说明：将一个固定块归还到内存池。 */
void osal_mempool_free(osal_mempool_t *mp, void *ptr) {
    uint8_t *block;
    uint8_t *start;
    uint8_t *end;
    uint32_t offset;
    uint32_t irq_state;

    if ((!osal_mempool_validate_handle(mp)) || (ptr == NULL)) {
        return;
    }

    block = (uint8_t *)ptr;
    start = mp->buf;
    end = mp->buf + (mp->block_count * mp->stride);
    if ((block < start) || (block >= end)) {
        osal_mem_report("mempool_free called with pointer outside mempool range");
        return;
    }

    offset = (uint32_t)(block - start);
    if ((offset % mp->stride) != 0U) {
        osal_mem_report("mempool_free called with misaligned block pointer");
        return;
    }

    irq_state = osal_irq_disable();
    *osal_block_next_ptr(block) = mp->free_list;
    mp->free_list = block;
    osal_irq_restore(irq_state);
}
