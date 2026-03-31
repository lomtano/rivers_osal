#ifndef OSAL_MEM_H
#define OSAL_MEM_H

#include <stdint.h>
#include "osal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct osal_mempool osal_mempool_t;

/*
 * 内存模块契约：
 * 1. osal_mem_alloc() 返回的块必须由 osal_mem_free() 归还。
 * 2. osal_mempool_create() 返回的句柄所有权归调用方，delete() 后句柄立即失效。
 * 3. osal_mempool_alloc() 返回的块必须归还给同一个 mempool。
 * 4. free(NULL) / delete(NULL) 是安全空操作。
 * 5. debug 打开时，可检测到的 double free、非法 mempool 句柄、错误归还块，
 *    会通过 OSAL_DEBUG_HOOK 报告。
 *
 * 接口能力矩阵：
 * - mem_init / mem_alloc / mem_free / mem_get_free_size: 任务态
 * - mempool_create / mempool_delete: 任务态
 * - mempool_alloc / mempool_free: 任务态 / ISR
 */

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
 * @brief 查询当前可用的堆空间。
 * @return 当前所有空闲块总大小，单位为字节。
 */
uint32_t osal_mem_get_free_size(void);

/**
 * @brief 基于用户缓冲区创建一个定长内存池。
 * @param pool_buffer 内存池底层缓冲区。
 * @param block_size 每个块的大小。
 * @param block_count 块数量。
 * @return 成功返回内存池句柄，失败返回 NULL。
 * @note pool_buffer 至少需要 max(block_size, sizeof(void *)) * block_count 字节。
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
