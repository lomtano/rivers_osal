/******************************************************************************
 * Copyright (C) 2024-2026 rivers. All rights reserved.
 *
 * @author JH
 *
 * @version V1.0 2023-12-03
 *
 * @note 1 tab == 4 spaces!
 *
 *****************************************************************************/

#ifndef OSAL_MEM_H
#define OSAL_MEM_H

#include <stdint.h>
#include "osal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct osal_mempool osal_mempool_t; // opaque

#ifndef OSAL_HEAP_SIZE
#define OSAL_HEAP_SIZE 4096U
#endif

/**
 * @brief Initialize the unified OSAL heap.
 * @param heap_buffer Optional user-provided heap buffer, NULL to use internal storage.
 * @param heap_size Size of heap_buffer in bytes, ignored when heap_buffer is NULL.
 */
void osal_mem_init(void *heap_buffer, uint32_t heap_size);

/**
 * @brief Allocate a block from the unified OSAL heap.
 * @param size Requested payload size in bytes.
 * @return Pointer to allocated memory, or NULL on failure.
 */
void *osal_mem_alloc(uint32_t size);

/**
 * @brief Return a block to the unified OSAL heap.
 * @param ptr Pointer returned by osal_mem_alloc().
 */
void osal_mem_free(void *ptr);

/**
 * @brief Query the currently available free heap space.
 * @return Total size of free blocks in bytes.
 */
uint32_t osal_mem_get_free_size(void);

// pool_buffer must be at least max(block_size, sizeof(void *)) * block_count bytes
/**
 * @brief Create a fixed-size memory pool on top of a user buffer.
 * @param pool_buffer Raw backing buffer for all pool blocks.
 * @param block_size Size of each pool block.
 * @param block_count Number of blocks in the pool.
 * @return Mempool handle, or NULL on failure.
 */
osal_mempool_t *osal_mempool_create(void *pool_buffer, uint32_t block_size, uint32_t block_count);

/**
 * @brief Destroy a memory pool control block.
 * @param mp Mempool handle.
 */
void osal_mempool_delete(osal_mempool_t *mp);

/**
 * @brief Allocate one block from a memory pool.
 * @param mp Mempool handle.
 * @return Pool block pointer, or NULL when empty.
 */
void *osal_mempool_alloc(osal_mempool_t *mp);

/**
 * @brief Return one block back to a memory pool.
 * @param mp Mempool handle.
 * @param ptr Block pointer originally returned by osal_mempool_alloc().
 */
void osal_mempool_free(osal_mempool_t *mp, void *ptr);

#ifdef __cplusplus
}
#endif

#endif // OSAL_MEM_H
