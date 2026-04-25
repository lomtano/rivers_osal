#include "../Inc/osal_mem.h"
#include "../Inc/osal_irq.h"
#include <stdbool.h>
#include <string.h>

#define OSAL_MEM_ALIGN           ((uint32_t)sizeof(void *))
/* size_and_flags 的最高位用来表示“当前块是否已分配”。 */
#define OSAL_BLOCK_USED_FLAG     (0x80000000UL)
/* 低 31 位保存块大小。 */
#define OSAL_BLOCK_SIZE_MASK     (0x7FFFFFFFUL)
/*
 * 拆分空闲块时保留下来的尾块至少要能放下“块头 + 一个最小净荷”。
 * 如果尾块太小，宁可整块分配出去，也不要制造几乎不可再利用的碎片。
 */
#define OSAL_HEAP_MIN_BLOCK_SIZE ((uint32_t)(sizeof(osal_heap_block_t) * 2U))

/*
 * 统一堆块头：
 * 1. size_and_flags 同时保存块大小和已分配标志。
 * 2. next_free 只在块处于空闲链表中时有意义。
 */
typedef struct osal_heap_block {
    uint32_t size_and_flags;
    struct osal_heap_block *next_free;
} osal_heap_block_t;

/*
 * 固定块内存池对象：
 * 1. buf 指向整块池缓冲区起始地址。
 * 2. block_size 是用户理解的块大小。
 * 3. stride 是实际步长，已经包含最小指针对齐要求。
 * 4. free_list 把每个空闲块串成单向链表。
 */
struct osal_mempool {
    uint8_t *buf;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t stride;
    void *free_list;
    struct osal_mempool *next;
};

/* 统一堆的底层缓冲区、总大小和空闲链表头。 */
static uint8_t *s_heap_buffer = NULL;
static uint32_t s_heap_size = 0U;
static osal_heap_block_t *s_free_list = NULL;
static bool s_heap_ready = false;
static uint32_t s_free_bytes = 0U;
static uint32_t s_min_ever_free_size = 0U;
static uint32_t s_alloc_count = 0U;
static uint32_t s_free_count = 0U;
static uint32_t s_alloc_fail_count = 0U;
/* 所有活动 mempool 的链表，仅用于句柄管理和调试校验。 */
static osal_mempool_t *s_mempool_list = NULL;

typedef union {
    void *align;
    uint8_t bytes[OSAL_HEAP_SIZE];
} osal_default_heap_t;

/* 当用户没有主动提供 heap_buffer 时，OSAL 默认使用这块内部静态堆。 */
static osal_default_heap_t s_default_heap;

/* 函数说明：输出内存模块调试诊断信息。 */
static void osal_mem_report(const char *message) {
    OSAL_DEBUG_REPORT("mem", message);
}

/* 函数说明：带溢出检查地向上对齐字节数。 */
static bool osal_mem_align_up_checked(uint32_t value, uint32_t *aligned_value) {
    uint32_t mask = OSAL_MEM_ALIGN - 1U;

    if (aligned_value == NULL) {
        return false;
    }
    if (value > (UINT32_MAX - mask)) {
        return false;
    }

    *aligned_value = (value + mask) & ~mask;
    return true;
}

/* 函数说明：将字节数向下对齐到系统对齐粒度。 */
static uint32_t osal_mem_align_down(uint32_t value) {
    /* 向下对齐直接把低位对齐余数清零。 */
    uint32_t mask = OSAL_MEM_ALIGN - 1U;
    return value & ~mask;
}

/* 函数说明：把堆起始地址向上对齐到指针对齐边界。 */
static uint8_t *osal_mem_align_ptr_up(uint8_t *ptr) {
    uintptr_t value = (uintptr_t)ptr;
    uintptr_t mask = (uintptr_t)(OSAL_MEM_ALIGN - 1U);
    return (uint8_t *)((value + mask) & ~mask);
}

/* 函数说明：根据用户净荷大小计算实际堆块大小，并检查加法和对齐溢出。 */
static bool osal_mem_make_request_size(uint32_t size, uint32_t *request_size) {
    uint32_t raw_size;
    uint32_t aligned_size;

    if ((size == 0U) || (request_size == NULL)) {
        return false;
    }
    if (size > (OSAL_BLOCK_SIZE_MASK - (uint32_t)sizeof(osal_heap_block_t))) {
        return false;
    }

    raw_size = size + (uint32_t)sizeof(osal_heap_block_t);
    if (!osal_mem_align_up_checked(raw_size, &aligned_size)) {
        return false;
    }
    if ((aligned_size < raw_size) || (aligned_size > OSAL_BLOCK_SIZE_MASK)) {
        return false;
    }

    *request_size = aligned_size;
    return true;
}

/* 函数说明：读取堆块当前记录的有效大小。 */
static uint32_t osal_heap_block_size(const osal_heap_block_t *block) {
    /* 最高位是“是否已分配”标志，因此取大小时要把最高位屏蔽掉。 */
    return (block->size_and_flags & OSAL_BLOCK_SIZE_MASK);
}

/* 函数说明：检查堆块是否处于已分配状态。 */
static bool osal_heap_block_used(const osal_heap_block_t *block) {
    /* 只要最高位被置 1，就表示这个块当前已经分配给用户。 */
    return ((block->size_and_flags & OSAL_BLOCK_USED_FLAG) != 0U);
}

/* 函数说明：更新堆块头中的大小和使用标志。 */
static void osal_heap_set_block(osal_heap_block_t *block, uint32_t size, bool used) {
    /* 这里统一负责把“大小”和“占用标志”重新打包回同一个 32 位字段。 */
    block->size_and_flags = (size & OSAL_BLOCK_SIZE_MASK) | (used ? OSAL_BLOCK_USED_FLAG : 0U);
}

/* 函数说明：把空闲块总大小换算成用户真正能申请到的最大净荷大小。 */
static uint32_t osal_heap_payload_capacity(uint32_t block_size) {
    if (block_size <= (uint32_t)sizeof(osal_heap_block_t)) {
        return 0U;
    }
    return block_size - (uint32_t)sizeof(osal_heap_block_t);
}

/* 函数说明：记录一次成功分配后的统计变化。 */
static void osal_mem_note_alloc(uint32_t allocated_block_size) {
    if (s_free_bytes >= allocated_block_size) {
        s_free_bytes -= allocated_block_size;
    } else {
        s_free_bytes = 0U;
    }

    if (s_free_bytes < s_min_ever_free_size) {
        s_min_ever_free_size = s_free_bytes;
    }
    ++s_alloc_count;
}

/* 函数说明：记录一次释放后的统计变化。 */
static void osal_mem_note_free(uint32_t freed_block_size) {
    if (freed_block_size > s_heap_size) {
        s_free_bytes = s_heap_size;
    } else if (s_free_bytes <= (s_heap_size - freed_block_size)) {
        s_free_bytes += freed_block_size;
    } else {
        s_free_bytes = s_heap_size;
    }
    ++s_free_count;
}

/* 函数说明：记录一次统一堆分配失败。 */
static void osal_mem_note_alloc_fail(void) {
    ++s_alloc_fail_count;
}

/* 函数说明：在临界区内记录一次分配失败，供提前返回路径使用。 */
static void osal_mem_note_alloc_fail_locked(void) {
    uint32_t irq_state = osal_internal_critical_enter();
    osal_mem_note_alloc_fail();
    osal_internal_critical_exit(irq_state);
}

/* 函数说明：获取固定块内下一指针字段的地址。 */
static void **osal_block_next_ptr(void *block) {
    /*
     * 固定块内存池没有单独的块头结构，
     * 因此直接借用每个空闲块起始处的前几个字节保存 next 指针。
     */
    return (void **)block;
}

/* 函数说明：确保统一 OSAL 堆已经完成初始化。 */
static void osal_mem_ensure_init(void) {
    if (!s_heap_ready) {
        /* 用户没显式初始化时，这里自动回退到内部默认静态堆。 */
        osal_mem_init(NULL, 0U);
    }
}

/* 函数说明：检查指针是否落在 OSAL 堆地址范围内。 */
static bool osal_mem_pointer_in_heap(const void *ptr) {
    const uint8_t *byte_ptr = (const uint8_t *)ptr;

    if ((!s_heap_ready) || (s_heap_buffer == NULL) || (s_heap_size == 0U) || (ptr == NULL)) {
        return false;
    }

    /* 这里判断的是“是否落在堆地址范围内”，不代表它一定是一个合法分配块。 */
    return ((byte_ptr >= s_heap_buffer) && (byte_ptr < (s_heap_buffer + s_heap_size)));
}

/* 函数说明：检查一个候选堆块头是否具有基本合法的大小和边界。 */
static bool osal_heap_block_header_is_valid(const osal_heap_block_t *block) {
    const uint8_t *block_ptr = (const uint8_t *)block;
    const uint8_t *heap_end;
    uint32_t block_size;
    uint32_t remaining;

    if (!osal_mem_pointer_in_heap(block)) {
        return false;
    }
    if ((((uintptr_t)block_ptr) & (uintptr_t)(OSAL_MEM_ALIGN - 1U)) != 0U) {
        return false;
    }

    heap_end = s_heap_buffer + s_heap_size;
    remaining = (uint32_t)(heap_end - block_ptr);
    block_size = osal_heap_block_size(block);
    if ((block_size < (uint32_t)sizeof(osal_heap_block_t)) || (block_size > remaining)) {
        return false;
    }
    if (osal_mem_align_down(block_size) != block_size) {
        return false;
    }

    return true;
}

/* 函数说明：将内存池对象挂入活动链表。 */
static void osal_mempool_link(osal_mempool_t *mp) {
    uint32_t irq_state;

    /* 内存池也维护一张活动链表，方便调试模式下检查句柄是否合法。 */
    irq_state = osal_internal_critical_enter();
    mp->next = s_mempool_list;
    s_mempool_list = mp;
    osal_internal_critical_exit(irq_state);
}

/* 函数说明：检查内存池句柄是否仍在活动链表中。 */
#if OSAL_CFG_ENABLE_DEBUG
static bool osal_mempool_contains_unlocked(osal_mempool_t *mp) {
    osal_mempool_t *current = s_mempool_list;

    while (current != NULL) {
        if (current == mp) {
            return true;
        }
        current = current->next;
    }

    return false;
}

/* 函数说明：在临界区内检查固定块是否已经位于 free_list 中。 */
static bool osal_mempool_free_list_contains_unlocked(const osal_mempool_t *mp, const void *ptr) {
    void *current;

    if ((mp == NULL) || (ptr == NULL)) {
        return false;
    }

    current = mp->free_list;
    while (current != NULL) {
        if (current == ptr) {
            return true;
        }
        current = *osal_block_next_ptr(current);
    }

    return false;
}
#endif

/* 函数说明：将内存池对象从活动链表中摘除。 */
static bool osal_mempool_unlink(osal_mempool_t *mp) {
    uint32_t irq_state;
    osal_mempool_t *prev = NULL;
    osal_mempool_t *current = s_mempool_list;
    bool removed = false;

    irq_state = osal_internal_critical_enter();
    while (current != NULL) {
        if (current == mp) {
            if (prev == NULL) {
                s_mempool_list = current->next;
            } else {
                prev->next = current->next;
            }
            current->next = NULL;
            removed = true;
            break;
        }
        prev = current;
        current = current->next;
    }
    osal_internal_critical_exit(irq_state);

    return removed;
}

/* 函数说明：校验内存池句柄是否有效。 */
static bool osal_mempool_validate_handle(const osal_mempool_t *mp) {
    if (mp == NULL) {
        return false;
    }
#if OSAL_CFG_ENABLE_DEBUG
    {
        uint32_t irq_state = osal_internal_critical_enter();
        bool valid = osal_mempool_contains_unlocked((osal_mempool_t *)mp);
        osal_internal_critical_exit(irq_state);

        if (!valid) {
            /* release 模式下不做这个遍历，debug 模式下才启用更严格的句柄校验。 */
            osal_mem_report("mempool API called with inactive mempool handle");
            return false;
        }
    }
#endif
    return true;
}

/* 函数说明：初始化统一 OSAL 堆区域。 */
void osal_mem_init(void *heap_buffer, uint32_t heap_size) {
    uint8_t *buffer;
    uint8_t *aligned_buffer;
    uint32_t size;
    uint32_t align_offset;
    uint32_t aligned_size;
    osal_heap_block_t *initial;

    if (osal_irq_is_in_isr()) {
        osal_mem_report("mem_init is not allowed in ISR context");
        return;
    }

    /*
     * 重新初始化统一堆意味着旧堆上的控制块都已经失效。
     * mempool 控制块也来自统一堆，所以这里同步清掉活动 mempool 链表。
     */
    s_mempool_list = NULL;

    if (heap_buffer != NULL && heap_size != 0U) {
        /* 用户显式给了外部堆，就优先用用户提供的这块缓冲区。 */
        buffer = (uint8_t *)heap_buffer;
        size = heap_size;
    } else {
        /* 否则回退到 OSAL 内部自带的静态大数组。 */
        buffer = s_default_heap.bytes;
        size = (uint32_t)sizeof(s_default_heap.bytes);
    }

    aligned_buffer = osal_mem_align_ptr_up(buffer);
    align_offset = (uint32_t)(aligned_buffer - buffer);
    if (size <= align_offset) {
        s_heap_buffer = NULL;
        s_heap_size = 0U;
        s_free_list = NULL;
        s_heap_ready = false;
        s_free_bytes = 0U;
        s_min_ever_free_size = 0U;
        return;
    }

    aligned_size = osal_mem_align_down(size - align_offset);
    if (aligned_size <= sizeof(osal_heap_block_t)) {
        /* 如果整块堆连一个块头都放不下，那这块堆就没有实际使用价值。 */
        s_heap_buffer = NULL;
        s_heap_size = 0U;
        s_free_list = NULL;
        s_heap_ready = false;
        s_free_bytes = 0U;
        s_min_ever_free_size = 0U;
        return;
    }

    s_heap_buffer = aligned_buffer;
    s_heap_size = aligned_size;
    /* 整块堆初始化时先视为“一个完整的大空闲块”。 */
    s_free_list = (osal_heap_block_t *)s_heap_buffer;
    initial = s_free_list;
    osal_heap_set_block(initial, s_heap_size, false);
    initial->next_free = NULL;
    s_heap_ready = true;
    s_free_bytes = s_heap_size;
    s_min_ever_free_size = s_heap_size;
    s_alloc_count = 0U;
    s_free_count = 0U;
    s_alloc_fail_count = 0U;
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
    if (osal_irq_is_in_isr()) {
        osal_mem_report("mem_alloc is not allowed in ISR context");
        return NULL;
    }

    /*
     * 用户请求的是“净荷大小”，真正分配时还要把块头算进去。
     * 同时按指针对齐，保证返回地址适合放常见对象和指针。
     */
    if (!osal_mem_make_request_size(size, &request_size)) {
        osal_mem_ensure_init();
        if (s_heap_ready) {
            osal_mem_note_alloc_fail_locked();
        }
        osal_mem_report("alloc size is too large");
        return NULL;
    }

    osal_mem_ensure_init();
    if (!s_heap_ready) {
        return NULL;
    }

    irq_state = osal_internal_critical_enter();
    if (request_size > s_heap_size) {
        osal_mem_note_alloc_fail();
        osal_internal_critical_exit(irq_state);
        osal_mem_report("alloc request exceeds heap size");
        return NULL;
    }

    prev = NULL;
    current = s_free_list;

    while (current != NULL) {
        uint32_t current_size = osal_heap_block_size(current);
        if (current_size >= request_size) {
            uint32_t remain = current_size - request_size;
            /*
             * 只有“剩余空间还能再容纳一个块头”时才拆块。
             * 否则把整个块直接分配掉，避免制造一个永远无法再次使用的碎片块。
             */
            if (remain >= OSAL_HEAP_MIN_BLOCK_SIZE) {
                /* 在当前块后半段切出一个新的空闲块。 */
                osal_heap_block_t *next = (osal_heap_block_t *)((uint8_t *)current + request_size);
                /* 新空闲块的大小就是“原块大小 - 本次分出去的大小”。 */
                osal_heap_set_block(next, remain, false);
                /* 新空闲块继承原来 current 在空闲链表里的后继。 */
                next->next_free = current->next_free;
                if (prev == NULL) {
                    /* 如果 current 原本是空闲链表头，就把新空闲块顶上去。 */
                    s_free_list = next;
                } else {
                    /* 否则把前驱节点改成指向新空闲块。 */
                    prev->next_free = next;
                }
                /* 当前块本身改成“已分配块”，大小记录为 request_size。 */
                osal_heap_set_block(current, request_size, true);
                osal_mem_note_alloc(request_size);
            } else {
                if (prev == NULL) {
                    s_free_list = current->next_free;
                } else {
                    prev->next_free = current->next_free;
                }
                /* 剩余空间太小就整块拿走，避免制造无用碎片。 */
                osal_heap_set_block(current, current_size, true);
                osal_mem_note_alloc(current_size);
            }
            /* 已分配块不应该再带着空闲链表指针。 */
            current->next_free = NULL;
            osal_internal_critical_exit(irq_state);
            /* 返回给用户的是块头之后的净荷地址。 */
            return (uint8_t *)current + sizeof(osal_heap_block_t);
        }
        /* 当前空闲块不够大，就继续向后找下一块。 */
        prev = current;
        current = current->next_free;
    }

    /* 整张空闲链表都找遍后仍没找到足够大的块，说明堆已经无法满足本次申请。 */
    osal_mem_note_alloc_fail();
    osal_internal_critical_exit(irq_state);
    osal_mem_report("alloc failed: no free block is large enough");
    return NULL;
}

/*
 * 释放后的空闲块必须按地址顺序插回链表：
 * 这样才能仅通过比较前后地址，判断是否与相邻空闲块物理连续并完成合并。
 */
/* 函数说明：按地址顺序插入一个空闲堆块并尝试合并。 */
static void osal_mem_insert_free_block(osal_heap_block_t *block) {
    osal_heap_block_t *prev = NULL;
    osal_heap_block_t *current = s_free_list;

    while (current != NULL && current < block) {
        /* 空闲链表保持按地址升序，后面的相邻合并逻辑才成立。 */
        prev = current;
        current = current->next_free;
    }

    block->next_free = current;
    if (prev == NULL) {
        /* 如果它比当前链表头地址更小，就直接成为新的空闲链表头。 */
        s_free_list = block;
    } else {
        prev->next_free = block;
    }

    if (block->next_free != NULL) {
        uint8_t *block_end = (uint8_t *)block + osal_heap_block_size(block);
        if (block_end == (uint8_t *)block->next_free) {
            /* 当前块和后继块物理上连续，直接向后合并。 */
            osal_heap_set_block(block, osal_heap_block_size(block) + osal_heap_block_size(block->next_free), false);
            block->next_free = block->next_free->next_free;
        }
    }

    if (prev != NULL) {
        uint8_t *prev_end = (uint8_t *)prev + osal_heap_block_size(prev);
        if (prev_end == (uint8_t *)block) {
            /* 前驱块和当前块物理连续，再向前合并，减少碎片。 */
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
    if (osal_irq_is_in_isr()) {
        osal_mem_report("mem_free is not allowed in ISR context");
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
    if (!osal_heap_block_header_is_valid(block)) {
        osal_mem_report("free called with corrupted heap block header");
        return;
    }
    if (!osal_heap_block_used(block)) {
        /* 这里能挡住最常见的二次释放或非法指针回收。 */
        osal_mem_report("double free or inactive heap block detected");
        return;
    }

    irq_state = osal_internal_critical_enter();
    /* 先把块状态改回“空闲”，再插回空闲链表。 */
    osal_mem_note_free(osal_heap_block_size(block));
    osal_heap_set_block(block, osal_heap_block_size(block), false);
    osal_mem_insert_free_block(block);
    osal_internal_critical_exit(irq_state);
}

/* 函数说明：在已进入临界区的前提下扫描最大连续空闲净荷和空闲块数量。 */
static uint32_t osal_mem_largest_free_payload_unlocked(uint32_t *free_block_count) {
    uint32_t largest = 0U;
    uint32_t count = 0U;
    osal_heap_block_t *current = s_free_list;

    while (current != NULL) {
        uint32_t payload = osal_heap_payload_capacity(osal_heap_block_size(current));
        if (payload > largest) {
            largest = payload;
        }
        ++count;
        current = current->next_free;
    }

    if (free_block_count != NULL) {
        *free_block_count = count;
    }
    return largest;
}

/* 函数说明：获取统一 OSAL 堆当前剩余的可用字节数。 */
uint32_t osal_mem_get_free_size(void) {
    uint32_t irq_state;
    uint32_t total;

    if (osal_irq_is_in_isr()) {
        osal_mem_report("mem_get_free_size is not allowed in ISR context");
        return 0U;
    }

    osal_mem_ensure_init();
    if (!s_heap_ready) {
        return 0U;
    }

    irq_state = osal_internal_critical_enter();
    /* 这里统计的是空闲块总大小，包含块头，不是纯净可用净荷大小。 */
    total = s_free_bytes;
    osal_internal_critical_exit(irq_state);

    return total;
}

/* 函数说明：获取当前最大连续可分配净荷大小。 */
uint32_t osal_mem_get_largest_free_block(void) {
    uint32_t irq_state;
    uint32_t largest;

    if (osal_irq_is_in_isr()) {
        osal_mem_report("mem_get_largest_free_block is not allowed in ISR context");
        return 0U;
    }

    osal_mem_ensure_init();
    if (!s_heap_ready) {
        return 0U;
    }

    irq_state = osal_internal_critical_enter();
    largest = osal_mem_largest_free_payload_unlocked(NULL);
    osal_internal_critical_exit(irq_state);

    return largest;
}

/* 函数说明：获取统一堆历史最小剩余空间。 */
uint32_t osal_mem_get_min_ever_free_size(void) {
    uint32_t irq_state;
    uint32_t min_free;

    if (osal_irq_is_in_isr()) {
        osal_mem_report("mem_get_min_ever_free_size is not allowed in ISR context");
        return 0U;
    }

    osal_mem_ensure_init();
    if (!s_heap_ready) {
        return 0U;
    }

    irq_state = osal_internal_critical_enter();
    min_free = s_min_ever_free_size;
    osal_internal_critical_exit(irq_state);

    return min_free;
}

/* 函数说明：读取统一堆统计信息。 */
void osal_mem_get_stats(osal_mem_stats_t *stats) {
    uint32_t irq_state;

    if (stats == NULL) {
        return;
    }

    memset(stats, 0, sizeof(*stats));
    if (osal_irq_is_in_isr()) {
        osal_mem_report("mem_get_stats is not allowed in ISR context");
        return;
    }

    osal_mem_ensure_init();
    if (!s_heap_ready) {
        return;
    }

    irq_state = osal_internal_critical_enter();
    stats->heap_size = s_heap_size;
    stats->free_size = s_free_bytes;
    stats->min_ever_free_size = s_min_ever_free_size;
    stats->largest_free_block = osal_mem_largest_free_payload_unlocked(&stats->free_block_count);
    stats->alloc_count = s_alloc_count;
    stats->free_count = s_free_count;
    stats->alloc_fail_count = s_alloc_fail_count;
    osal_internal_critical_exit(irq_state);
}

/* 函数说明：根据用户提供缓冲区创建一个固定块内存池。 */
osal_mempool_t *osal_mempool_create(void *pool_buffer, uint32_t block_size, uint32_t block_count) {
    osal_mempool_t *mp;
    uint32_t stride;
    uint32_t block;

    if (osal_irq_is_in_isr()) {
        osal_mem_report("mempool_create is not allowed in ISR context");
        return NULL;
    }
    if ((pool_buffer == NULL) || (block_size == 0U) || (block_count == 0U)) {
        return NULL;
    }
    if ((((uintptr_t)pool_buffer) & (uintptr_t)(OSAL_MEM_ALIGN - 1U)) != 0U) {
        osal_mem_report("mempool_create requires pointer-aligned pool_buffer");
        return NULL;
    }

    /*
     * stride 是每个固定块的实际步长，至少要能放下一个 next 指针。
     * 这里先把所有乘法和对齐风险检查完，再申请 mempool 控制块，避免失败路径泄漏。
     */
    if (block_size < (uint32_t)sizeof(void *)) {
        stride = (uint32_t)sizeof(void *);
    } else if (!osal_mem_align_up_checked(block_size, &stride)) {
        osal_mem_report("mempool block size overflow");
        return NULL;
    }
    if (block_count > (UINT32_MAX / stride)) {
        osal_mem_report("mempool total size overflow");
        return NULL;
    }

    mp = (osal_mempool_t *)osal_mem_alloc((uint32_t)sizeof(osal_mempool_t));
    if (mp == NULL) {
        return NULL;
    }

    mp->buf = (uint8_t *)pool_buffer;
    mp->block_size = block_size;
    mp->block_count = block_count;
    /*
     * 每个块的真实步长 stride 需要至少能放下一个 next 指针，
     * 否则 free_list 无法把空闲块串起来。
     */
    mp->stride = stride;
    mp->free_list = pool_buffer;
    mp->next = NULL;

    for (block = 0U; block < block_count; ++block) {
        uint8_t *current = mp->buf + (block * mp->stride);
        uint8_t *next = (block + 1U < block_count) ? (mp->buf + ((block + 1U) * mp->stride)) : NULL;
        /* 把每个块的起始地址前几个字节临时当作 next 指针，串成单向空闲链表。 */
        *osal_block_next_ptr(current) = next;
    }

    /* 至此整个固定块池已经串成一条完整的 free_list。 */
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
    /* 这里只删除“内存池控制块”，用户提供的 pool_buffer 仍由用户自己管理。 */
    osal_mem_free(mp);
}

/* 函数说明：从固定块内存池中申请一个块。 */
void *osal_mempool_alloc(osal_mempool_t *mp) {
    void *block;
    uint32_t irq_state;

    if (!osal_mempool_validate_handle(mp)) {
        return NULL;
    }

    irq_state = osal_internal_critical_enter();
    block = mp->free_list;
    if (block == NULL) {
        /* free_list 为空，说明所有固定块都已经被分配出去了。 */
        osal_internal_critical_exit(irq_state);
        return NULL;
    }
    /* free_list 的头节点就是本次要分配出去的块。 */
    mp->free_list = *osal_block_next_ptr(block);
    osal_internal_critical_exit(irq_state);
    /* 返回的是块本体地址，不额外带块头。 */
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
        /* 只有恰好落在每个块起始地址上的指针，才是合法的固定块指针。 */
        osal_mem_report("mempool_free called with misaligned block pointer");
        return;
    }

    irq_state = osal_internal_critical_enter();
#if OSAL_CFG_ENABLE_DEBUG
    if (osal_mempool_free_list_contains_unlocked(mp, block)) {
        osal_internal_critical_exit(irq_state);
        osal_mem_report("mempool_free detected double free block");
        return;
    }
#endif
    /* 头插回 free_list，归还操作就是 O(1)。 */
    *osal_block_next_ptr(block) = mp->free_list;
    mp->free_list = block;
    osal_internal_critical_exit(irq_state);
}




