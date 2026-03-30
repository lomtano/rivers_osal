#ifndef OSAL_MEM_H
#define OSAL_MEM_H

#include <stdint.h>
#include "osal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct osal_mempool osal_mempool_t; /* 不透明内存池句柄 */

#ifndef OSAL_HEAP_SIZE
#define OSAL_HEAP_SIZE 4096U
#endif

/**
 * @brief 初始化统一的 OSAL 静态堆。
 * @param heap_buffer 用户自定义堆缓冲区，传 NULL 时使用内部静态数组。
 * @param heap_size heap_buffer 的大小，单位为字节；当 heap_buffer 为 NULL 时忽略。
 */
void osal_mem_init(void *heap_buffer, uint32_t heap_size);

/**
 * @brief 从统一的 OSAL 静态堆中申请一块内存。
 * @param size 申请大小，单位为字节。
 * @return 成功返回内存指针，失败返回 NULL。
 */
void *osal_mem_alloc(uint32_t size);

/**
 * @brief 将一块内存归还到统一的 OSAL 静态堆。
 * @param ptr 由 osal_mem_alloc() 返回的指针。
 */
void osal_mem_free(void *ptr);

/**
 * @brief 查询当前可用的空闲堆空间。
 * @return 当前所有空闲块总大小，单位为字节。
 */
uint32_t osal_mem_get_free_size(void);

/* pool_buffer 至少需要 max(block_size, sizeof(void *)) * block_count 字节。 */
/**
 * @brief 基于用户缓冲区创建一个定长内存池。
 * @param pool_buffer 内存池底层缓冲区。
 * @param block_size 每个块的大小。
 * @param block_count 块数量。
 * @return 成功返回内存池句柄，失败返回 NULL。
 */
osal_mempool_t *osal_mempool_create(void *pool_buffer, uint32_t block_size, uint32_t block_count);

/**
 * @brief 销毁一个内存池控制块。
 * @param mp 内存池句柄。
 */
void osal_mempool_delete(osal_mempool_t *mp);

/**
 * @brief 从内存池中申请一个块。
 * @param mp 内存池句柄。
 * @return 成功返回块指针，空池时返回 NULL。
 */
void *osal_mempool_alloc(osal_mempool_t *mp);

/**
 * @brief 将一个块归还到内存池。
 * @param mp 内存池句柄。
 * @param ptr 由 osal_mempool_alloc() 返回的块指针。
 */
void osal_mempool_free(osal_mempool_t *mp, void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_MEM_H */
