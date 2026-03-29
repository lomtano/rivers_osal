/*
 * osal_mem.c
 * Unified static heap allocator plus fixed-size mempool support.
 * - Heap allocator: first-fit free list with adjacent block coalescing
 * - Control objects in other OSAL modules can allocate from this heap
 * - User may provide a custom heap buffer, otherwise OSAL_HEAP_SIZE is used
 */

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
};

static uint8_t *s_heap_buffer = NULL;
static uint32_t s_heap_size = 0U;
static osal_heap_block_t *s_free_list = NULL;
static bool s_heap_ready = false;

typedef union {
    void *align;
    uint8_t bytes[OSAL_HEAP_SIZE];
} osal_default_heap_t;

static osal_default_heap_t s_default_heap;

/* Round a byte count upward to the allocator alignment boundary. */
static uint32_t osal_mem_align_up(uint32_t value) {
    uint32_t mask = OSAL_MEM_ALIGN - 1U;
    return (value + mask) & ~mask;
}

/* Round a byte count downward to the allocator alignment boundary. */
static uint32_t osal_mem_align_down(uint32_t value) {
    uint32_t mask = OSAL_MEM_ALIGN - 1U;
    return value & ~mask;
}

/* Read the raw size field from one heap block header. */
static uint32_t osal_heap_block_size(const osal_heap_block_t *block) {
    return (block->size_and_flags & OSAL_BLOCK_SIZE_MASK);
}

/* Check whether one heap block is currently allocated. */
static bool osal_heap_block_used(const osal_heap_block_t *block) {
    return ((block->size_and_flags & OSAL_BLOCK_USED_FLAG) != 0U);
}

/* Write the size and allocation flag into one heap block header. */
static void osal_heap_set_block(osal_heap_block_t *block, uint32_t size, bool used) {
    block->size_and_flags = (size & OSAL_BLOCK_SIZE_MASK) | (used ? OSAL_BLOCK_USED_FLAG : 0U);
}

/* Reuse the first word of each mempool block as a singly-linked next pointer. */
static void **osal_block_next_ptr(void *block) {
    return (void **)block;
}

/* Lazily initialize the heap the first time any allocator API is used. */
static void osal_mem_ensure_init(void) {
    if (!s_heap_ready) {
        osal_mem_init(NULL, 0U);
    }
}

/* Initialize the unified OSAL heap from either user or default storage. */
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

/* Allocate one block from the unified OSAL heap using first-fit search. */
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

/* Insert a freed block back into the ordered free list and coalesce neighbors. */
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

/* Return one previously allocated block back to the unified OSAL heap. */
void osal_mem_free(void *ptr) {
    uint32_t irq_state;
    osal_heap_block_t *block;

    if (ptr == NULL) {
        return;
    }

    osal_mem_ensure_init();
    if (!s_heap_ready) {
        return;
    }

    block = (osal_heap_block_t *)((uint8_t *)ptr - sizeof(osal_heap_block_t));
    if (!osal_heap_block_used(block)) {
        return;
    }

    irq_state = osal_irq_disable();
    osal_heap_set_block(block, osal_heap_block_size(block), false);
    osal_mem_insert_free_block(block);
    osal_irq_restore(irq_state);
}

/* Sum the total size of all free heap blocks. */
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

/* Allocate a mempool control block from the unified OSAL heap. */
osal_mempool_t *osal_mempool_create(void *pool_buffer, uint32_t block_size, uint32_t block_count) {
    osal_mempool_t *mp;

    if (!pool_buffer || block_size == 0U || block_count == 0U) {
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

    for (uint32_t block = 0U; block < block_count; ++block) {
        uint8_t *current = mp->buf + (block * mp->stride);
        uint8_t *next = (block + 1U < block_count) ? (mp->buf + ((block + 1U) * mp->stride)) : NULL;
        *osal_block_next_ptr(current) = next;
    }

    return mp;
}

/* Destroy a mempool control block. */
void osal_mempool_delete(osal_mempool_t *mp) {
    if (mp == NULL) {
        return;
    }
    osal_mem_free(mp);
}

/* Pop one fixed-size block from a mempool free list. */
void *osal_mempool_alloc(osal_mempool_t *mp) {
    void *block;
    uint32_t irq_state;

    if (mp == NULL || mp->free_list == NULL) {
        return NULL;
    }

    irq_state = osal_irq_disable();
    block = mp->free_list;
    mp->free_list = *osal_block_next_ptr(block);
    osal_irq_restore(irq_state);
    return block;
}

/* Push one fixed-size block back into a mempool free list. */
void osal_mempool_free(osal_mempool_t *mp, void *ptr) {
    uint8_t *block;
    uint8_t *start;
    uint8_t *end;
    uint32_t offset;
    uint32_t irq_state;

    if (mp == NULL || ptr == NULL) {
        return;
    }

    block = (uint8_t *)ptr;
    start = mp->buf;
    end = mp->buf + (mp->block_count * mp->stride);
    if (block < start || block >= end) {
        return;
    }

    offset = (uint32_t)(block - start);
    if ((offset % mp->stride) != 0U) {
        return;
    }

    irq_state = osal_irq_disable();
    *osal_block_next_ptr(block) = mp->free_list;
    mp->free_list = block;
    osal_irq_restore(irq_state);
}
