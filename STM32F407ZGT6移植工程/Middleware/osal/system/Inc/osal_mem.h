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
 * 5. 统一堆接口只允许任务态调用；固定块池 alloc/free 支持任务态和 ISR。
 * 6. debug 打开时，可检测到的 double free、非法 mempool 句柄、错误归还块，
 *    会通过 OSAL_DEBUG_HOOK 报告。
 * 7. 统一堆接口如果在 ISR 中误用，会直接返回失败/0；debug 打开时额外报告错误。
 *
 * 接口能力矩阵：
 * - mem_init / mem_alloc / mem_free / mem_get_free_size /
 *   mem_get_largest_free_block / mem_get_min_ever_free_size / mem_get_stats: 任务态
 * - mempool_create / mempool_delete: 任务态
 * - mempool_alloc / mempool_free: 任务态 / ISR
 */

#ifndef OSAL_HEAP_SIZE
#define OSAL_HEAP_SIZE 4096U
#endif
/* 默认统一堆大小，只有在没有传入外部 heap_buffer 时才会使用这块内部数组。 */

typedef struct {
    /* 当前统一堆总大小，已经按指针对齐修正。 */
    uint32_t heap_size;
    /* 当前所有空闲块总大小，包含空闲块头开销。 */
    uint32_t free_size;
    /* 系统运行以来 free_size 的最低水位。 */
    uint32_t min_ever_free_size;
    /* 当前最大连续可分配净荷大小，不包含内部块头。 */
    uint32_t largest_free_block;
    /* 当前空闲链表上的块数量，可用于观察碎片化趋势。 */
    uint32_t free_block_count;
    /* 统一堆成功分配次数。 */
    uint32_t alloc_count;
    /* 统一堆成功释放次数。 */
    uint32_t free_count;
    /* 统一堆分配失败次数，包括溢出、超过堆大小和找不到足够大空闲块。 */
    uint32_t alloc_fail_count;
} osal_mem_stats_t;

/**
 * @brief 初始化统一的 OSAL 静态堆。
 * @param heap_buffer 用户自定义堆缓冲区，传 NULL 时使用内部静态数组。
 * @param heap_size heap_buffer 的大小，单位为字节；当 heap_buffer 为 NULL 时忽略。
 * @note 如果你完全不调用本函数，OSAL 会在第一次分配时自动回退到内部默认静态堆。
 */
void osal_mem_init(void *heap_buffer, uint32_t heap_size);

/**
 * @brief 从统一的 OSAL 静态堆中申请一块内存。
 * @param size 申请大小，单位为字节。
 * @return 成功返回内存指针，失败返回 NULL。
 * @note 返回的是“可给用户直接使用的净荷地址”，不是内部块头地址。
 * @note 只允许任务态调用；ISR 中请使用固定块内存池或预分配缓冲区。
 */
void *osal_mem_alloc(uint32_t size);

/**
 * @brief 将一块内存归还到统一的 OSAL 静态堆。
 * @param ptr 由 osal_mem_alloc() 返回的指针。
 * @note 传入 NULL 是安全空操作。
 * @note debug 打开时，常见的二次释放和越界指针会被报告。
 * @note 只允许任务态调用；ISR 中归还固定大小对象请使用 osal_mempool_free()。
 */
void osal_mem_free(void *ptr);

/**
 * @brief 查询当前可用的堆空间。
 * @return 当前所有空闲块总大小，单位为字节。
 * @note 返回值包含空闲块头开销，因此不等同于“纯净可分配净荷大小”。
 */
uint32_t osal_mem_get_free_size(void);

/**
 * @brief 查询当前最大连续可分配净荷大小。
 * @return 当前最大空闲块扣除内部块头后的净荷大小，单位为字节。
 * @note 这个值比 free_size 更适合判断“一次大块申请”是否可能成功。
 */
uint32_t osal_mem_get_largest_free_block(void);

/**
 * @brief 查询系统运行以来统一堆的历史最小剩余空间。
 * @return 历史最小空闲块总大小，单位为字节。
 * @note 统计口径与 osal_mem_get_free_size() 一致，包含空闲块头开销。
 */
uint32_t osal_mem_get_min_ever_free_size(void);

/**
 * @brief 读取统一堆统计信息。
 * @param stats 输出统计结构体。
 * @note stats 为 NULL 时安全空操作。
 */
void osal_mem_get_stats(osal_mem_stats_t *stats);

/**
 * @brief 基于用户缓冲区创建一个定长内存池。
 * @param pool_buffer 内存池底层缓冲区。
 * @param block_size 每个块的大小。
 * @param block_count 块数量。
 * @return 成功返回内存池句柄，失败返回 NULL。
 * @note pool_buffer 至少需要 align_up(max(block_size, sizeof(void *))) * block_count 字节。
 * @note pool_buffer 必须满足指针对齐，因为空闲块起始处会临时保存 next 指针。
 * @note pool_buffer 的生命周期由调用方负责，OSAL 不会代为释放。
 */
osal_mempool_t *osal_mempool_create(void *pool_buffer, uint32_t block_size, uint32_t block_count);

/**
 * @brief 销毁一个内存池控制块。
 * @param mp 内存池句柄。
 * @note 这里只释放“内存池控制块”，不会释放用户传入的 pool_buffer。
 */
void osal_mempool_delete(osal_mempool_t *mp);

/**
 * @brief 从内存池中申请一个块。
 * @param mp 内存池句柄。
 * @return 成功返回块指针，空池时返回 NULL。
 * @note 返回的块大小固定为创建时指定的 block_size。
 * @note 该接口支持任务态和 ISR。
 */
void *osal_mempool_alloc(osal_mempool_t *mp);

/**
 * @brief 将一个块归还到内存池。
 * @param mp 内存池句柄。
 * @param ptr 由 osal_mempool_alloc() 返回的块指针。
 * @note ptr 必须归还给“同一个 mempool”，不能跨池归还。
 * @note 该接口支持任务态和 ISR；debug 打开时会扫描 free_list 检测可见的二次归还。
 */
void osal_mempool_free(osal_mempool_t *mp, void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_MEM_H */




