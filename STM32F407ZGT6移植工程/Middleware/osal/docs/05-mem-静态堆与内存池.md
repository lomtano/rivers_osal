# mem：静态堆与固定块内存池

`osal_mem` 是 OSAL 的静态内存管理模块。它不调用 C 标准库 `malloc/free`，而是在静态 RAM 上自己维护分配状态。

当前模块包含两套机制：

- 统一静态堆
- 基于用户缓冲区的定长内存池

一句话总结：

> 统一堆负责灵活，固定块池负责实时。

## 1. 模块职责

`mem` 提供两类内存能力：

- 统一静态堆
- 基于用户缓冲区的定长内存池

当前 OSAL 的 `task`、`queue`、`timer` 等对象，默认都依赖统一静态堆分配。

整体结构可以理解为：

```text
osal_mem
|
|-- 统一静态堆 heap
|   |-- 默认内部 4096 字节静态数组
|   |-- 支持用户传入外部 heap_buffer 替换默认堆
|   |-- osal_mem_alloc()
|   |-- osal_mem_free()
|   |-- osal_mem_get_free_size()
|   |-- osal_mem_get_largest_free_block()
|   |-- osal_mem_get_min_ever_free_size()
|   `-- osal_mem_get_stats()
|
`-- 固定块内存池 mempool
    |-- 由用户提供 pool_buffer
    |-- 每块大小固定
    |-- osal_mempool_create()
    |-- osal_mempool_alloc()
    |-- osal_mempool_free()
    `-- osal_mempool_delete()
```

## 2. 统一静态堆

### 2.1 对外接口

统一静态堆接口包括：

- `osal_mem_init()`
- `osal_mem_alloc()`
- `osal_mem_free()`
- `osal_mem_get_free_size()`
- `osal_mem_get_largest_free_block()`
- `osal_mem_get_min_ever_free_size()`
- `osal_mem_get_stats()`

其中前四个是最基础的初始化、申请、释放和剩余空间查询接口；后三个用于观察碎片、历史最低水位和分配失败次数。

### 2.2 使用规则

- `osal_mem_alloc()` 返回的块必须用 `osal_mem_free()` 归还。
- `osal_mem_free(NULL)` 是安全空操作。
- 如果没有主动调用 `osal_mem_init()`，模块会在首次分配时回退到内部默认静态堆。
- `osal_mem_get_free_size()` 返回的是空闲块总大小，包含内部块头，不等于最大可申请净荷。
- `osal_mem_get_largest_free_block()` 才是判断“一次大块申请是否可能成功”的更直接指标。

### 2.3 当前用途

当前这些对象都直接使用统一静态堆：

- 任务控制块
- 队列控制块
- 队列数据区
- 软件定时器控制块
- 组件内部需要的动态对象

### 2.4 它解决什么问题

统一静态堆解决的是：

> 我有一整块静态内存，比如默认 4096 字节，但希望像 `malloc/free` 一样按需申请不同大小的内存。

所以它适合：

- 初始化阶段创建对象
- 低频申请/释放
- 大小不固定的数据结构
- 任务态使用

不适合：

- ISR 中申请或释放
- 高频实时路径反复申请释放
- 对最坏执行时间要求很硬的控制环

当前实现已经把统一堆接口限定为任务态：如果在 ISR 中误调 `osal_mem_init()`、`osal_mem_alloc()`、`osal_mem_free()` 或统计查询接口，函数会直接返回失败值，不会进入堆链表扫描；debug 打开时还会通过调试钩子报告错误。

### 2.5 默认 4096 字节静态堆是怎么来的

默认堆大小由 `OSAL_HEAP_SIZE` 决定：

```c
#ifndef OSAL_HEAP_SIZE
#define OSAL_HEAP_SIZE 4096U
#endif
```

源码里定义了：

```c
typedef union {
    void *align;
    uint8_t bytes[OSAL_HEAP_SIZE];
} osal_default_heap_t;

static osal_default_heap_t s_default_heap;
```

这个 `union` 的作用是：

- `bytes[OSAL_HEAP_SIZE]` 是真正的默认内存区。
- `void *align` 让这块内存至少满足指针对齐。

所以默认情况下，它就是在内部 4096 字节静态数组上做动态分配。

### 2.6 为什么 `osal_mem_init()` 还允许用户传 `heap_buffer`

这是为了扩展，不是必须。

```c
void osal_mem_init(void *heap_buffer, uint32_t heap_size);
```

如果你传：

```c
osal_mem_init(NULL, 0U);
```

它用内部默认静态堆。

如果你传：

```c
static uint8_t my_heap[8192];
osal_mem_init(my_heap, sizeof(my_heap));
```

它就改用你提供的 `my_heap`。

源码逻辑是：如果 `heap_buffer != NULL && heap_size != 0U`，优先用用户提供的外部堆；否则回退到 `s_default_heap.bytes`。

因此：

> 默认内部堆够用时不用管；想放到指定 RAM 区域、想扩大堆空间时，才传外部 buffer。

比如 STM32 上你可能想把 OSAL 堆放到 CCMRAM、DTCM 或外部 SRAM，这个接口就有用。

当前实现还会把外部 `heap_buffer` 起始地址向上对齐到指针对齐边界，并把可用大小向下对齐，避免外部 buffer 地址不对齐导致块头或返回指针不安全。

### 2.7 核心算法

统一堆使用：

> 块头 + 空闲链表 + 分裂 + 释放合并

它和 FreeRTOS `heap_4` 思路比较接近。

每个堆块都有一个块头：

```c
typedef struct osal_heap_block {
    uint32_t size_and_flags;
    struct osal_heap_block *next_free;
} osal_heap_block_t;
```

其中：

- `size_and_flags` 同时保存块大小和是否已分配。
- 最高位表示当前块是否已分配。
- 低 31 位表示块大小。
- `next_free` 只有当前块在空闲链表中时才有意义。

相关宏：

```c
#define OSAL_BLOCK_USED_FLAG  (0x80000000UL)
#define OSAL_BLOCK_SIZE_MASK  (0x7FFFFFFFUL)
```

真实内存布局是：

```text
[ 块头 osal_heap_block_t ][ 用户可用数据区 ]
```

用户拿到的是块头后面的净荷地址，而不是块头本身。

### 2.8 初始化过程

`osal_mem_init()` 初始化时，会把整块堆区域看成一个完整的大空闲块：

```text
s_free_list
   |
   v
[ 一个大空闲块 ]
```

同时初始化：

- `s_heap_buffer`
- `s_heap_size`
- `s_free_list`
- 初始块头
- `s_heap_ready = true`
- `s_free_bytes = s_heap_size`
- `s_min_ever_free_size = s_heap_size`
- `alloc_count/free_count/alloc_fail_count = 0`

重新初始化统一堆会清掉活动 mempool 链表，因为 mempool 控制块本身也来自统一堆。

### 2.9 分配与分裂

`osal_mem_alloc(size)` 的流程是：

1. `size == 0` 直接返回 `NULL`。
2. 检查 `size + block_header` 和对齐过程是否溢出。
3. 确保堆已初始化。
4. 把用户申请大小加上块头大小。
5. 做指针对齐。
6. 进入临界区。
7. 遍历空闲链表。
8. 找到第一个足够大的空闲块。
9. 如果剩余空间够放一个有效空闲块，就拆块。
10. 否则整块分配出去。
11. 更新统计信息，返回块头后的净荷地址。

分裂示意：

```text
申请前：
[ 空闲 1024B ]

申请后：
[ 已分配块 ][ 新空闲块 ]
```

当前分裂阈值是：

```c
#define OSAL_HEAP_MIN_BLOCK_SIZE (sizeof(osal_heap_block_t) * 2U)
```

也就是说，剩余尾块至少要能放下“一个块头 + 一个最小净荷”。如果尾块太小，直接把整块分配出去，避免制造几乎不可再利用的小碎片。

### 2.10 释放与合并

`osal_mem_free(ptr)` 的流程是：

1. `ptr == NULL` 直接返回。
2. 检查堆是否初始化。
3. 检查用户指针是否在 OSAL 堆范围内。
4. 根据用户指针反推块头地址。
5. 检查块头边界、对齐和大小是否基本合法。
6. 检查块是否处于已分配状态。
7. 进入临界区。
8. 把块标记为空闲。
9. 按地址顺序插回空闲链表。
10. 尝试和前后相邻空闲块合并。

按地址顺序插回链表是合并的前提。只有链表按地址升序排列，释放时才能直接判断：

```text
prev_end == current_start
current_end == next_start
```

如果物理地址连续，就合并成更大的空闲块。

合并的目的：

> 减少外部碎片，提高后续大块分配成功率。

### 2.11 统计接口

`osal_mem_get_free_size()` 返回当前所有空闲块总大小，单位字节。这个值包含空闲块头开销。

`osal_mem_get_largest_free_block()` 返回当前最大连续可分配净荷大小。这个值更适合判断一次大块申请是否可能成功。

`osal_mem_get_min_ever_free_size()` 返回系统运行以来统一堆的历史最小剩余空间，用于评估堆峰值压力，类似 FreeRTOS 的 `xPortGetMinimumEverFreeHeapSize()`。

`osal_mem_get_stats()` 返回完整统计结构体：

```c
typedef struct {
    uint32_t heap_size;
    uint32_t free_size;
    uint32_t min_ever_free_size;
    uint32_t largest_free_block;
    uint32_t free_block_count;
    uint32_t alloc_count;
    uint32_t free_count;
    uint32_t alloc_fail_count;
} osal_mem_stats_t;
```

字段含义：

- `heap_size`：当前统一堆总大小，已按对齐修正。
- `free_size`：当前空闲块总大小，包含空闲块头。
- `min_ever_free_size`：历史最小空闲块总大小。
- `largest_free_block`：当前最大连续可分配净荷大小。
- `free_block_count`：当前空闲链表中的块数量。
- `alloc_count`：统一堆成功分配次数。
- `free_count`：统一堆成功释放次数。
- `alloc_fail_count`：统一堆分配失败次数。

## 3. 定长内存池

### 3.1 对外接口

定长内存池接口包括：

- `osal_mempool_create()`
- `osal_mempool_delete()`
- `osal_mempool_alloc()`
- `osal_mempool_free()`

### 3.2 适用场景

内存池解决的是：

> 我有一批大小一样的小对象，希望申请和释放非常快、非常可控。

内存池更适合：

- 对象大小固定
- 申请释放频繁
- 希望在 ISR 中做固定块申请/归还
- 消息块
- 协议帧节点
- 固定大小结构体
- 不希望走统一堆扫描的实时路径

### 3.3 约束

- `pool_buffer` 生命周期由调用方负责。
- `pool_buffer` 必须满足指针对齐，因为空闲块起始处会临时保存 `next` 指针。
- `pool_buffer` 至少需要 `align_up(max(block_size, sizeof(void *))) * block_count` 字节。
- `osal_mempool_alloc()` 返回的块必须归还给同一个内存池。
- `osal_mempool_delete()` 只删除控制块，不会释放用户传入的底层缓冲区。

### 3.4 为什么固定块池需要用户提供 `pool_buffer`

固定块池不是默认从 4096B 统一堆里切数据区，而是要求用户传进来一块底层 buffer：

```c
static uint8_t msg_pool_buf[16][32];

osal_mempool_t *msg_pool =
    osal_mempool_create(msg_pool_buf, 32U, 16U);
```

这里：

- `msg_pool_buf` 是真正放 16 个消息块的地方。
- `osal_mempool_create()` 只是把这块内存组织成一个 `free_list`。

### 3.5 固定块池控制块从哪里来

`osal_mempool_t` 控制块本身，是通过统一堆申请的：

```c
mp = (osal_mempool_t *)osal_mem_alloc(sizeof(osal_mempool_t));
```

但固定块池真正分配给用户的数据块，来自用户传入的 `pool_buffer`。

关系是：

```text
统一堆
    `-- osal_mempool_t 控制块

用户 pool_buffer
    `-- N 个固定大小数据块
```

### 3.6 固定块池算法原理

固定块池没有每个块独立的块头。

它直接借用空闲块起始处的前几个字节保存 `next` 指针：

```c
static void **osal_block_next_ptr(void *block)
{
    return (void **)block;
}
```

初始化后：

```text
free_list
   |
   v
[ block0 | next ] -> [ block1 | next ] -> [ block2 | next ] -> NULL
```

分配时：

```text
block = free_list
free_list = block->next
return block
```

释放时：

```text
block->next = free_list
free_list = block
```

所以固定块池的申请和释放都是 `O(1)`。

## 4. 两套机制的区别

| 项目 | 统一静态堆 `osal_mem_alloc/free` | 固定块池 `osal_mempool_alloc/free` |
| --- | --- | --- |
| 底层内存来源 | 默认内部 4096B，也可用户传外部 heap | 用户传入 `pool_buffer` |
| 分配大小 | 可变大小 | 固定大小 |
| 算法 | 空闲链表 + 分裂 + 合并 | 固定块 free list |
| 分配耗时 | 需要遍历空闲链表，非固定 | O(1) |
| 碎片问题 | 可能有外部碎片，释放会合并 | 无外部碎片，有内部浪费 |
| 是否适合 ISR | 不允许 | 可以 |
| 适合对象 | 低频、大小不一 | 高频、固定大小 |

## 5. 接口上下文边界

统一堆：

- `osal_mem_init()`：任务态
- `osal_mem_alloc()`：任务态
- `osal_mem_free()`：任务态
- `osal_mem_get_free_size()`：任务态
- `osal_mem_get_largest_free_block()`：任务态
- `osal_mem_get_min_ever_free_size()`：任务态
- `osal_mem_get_stats()`：任务态

这些统一堆接口如果在 ISR 中被调用，会直接返回失败值；debug 打开时会额外报告错误。这样做是为了避免在中断里执行非固定耗时的空闲链表遍历、分裂和合并。

内存池：

- `osal_mempool_create()`：任务态
- `osal_mempool_delete()`：任务态
- `osal_mempool_alloc()`：任务态 / ISR
- `osal_mempool_free()`：任务态 / ISR

## 6. 与当前 OSAL 模型的关系

当前 `queue` 已经不再支持用户自带静态消息缓冲区创建方式，因此：

- 如果你只是使用 OSAL 队列，通常直接依赖统一静态堆即可。
- 如果你的业务确实需要严格固定块管理，可以在应用层单独使用 `osal_mempool`。

更具体地说：

- OSAL 内部对象优先走统一堆，因为这些对象大多在初始化阶段创建，大小也不完全一致。
- 高频、固定大小、ISR 可能参与的业务对象，应该由应用层自己创建固定块池。
- 统一堆不是实时路径的万能分配器；固定块池也不是通用变长分配器。

## 7. 调试行为

当 `OSAL_CFG_ENABLE_DEBUG` 打开时，模块会通过 `OSAL_DEBUG_HOOK` 报告可检测到的问题：

- 统一堆分配大小溢出
- 统一堆申请超过堆总大小
- 统一堆没有足够大的连续空闲块
- 二次释放
- 非法堆指针
- 损坏的堆块头
- ISR 中误用统一堆接口
- 非法 mempool 句柄
- 未对齐的 `pool_buffer`
- 错误归还到别的 mempool
- 固定块池可见的二次归还

这些报告是调试辅助，不替代调用方遵守接口约束。

固定块池的二次归还是通过扫描当前 `free_list` 检测的：如果要归还的块已经在空闲链表里，debug 构建会报告 `mempool_free detected double free block` 并拒绝再次插入。release 构建不做这类遍历，以保持 ISR 路径轻量。

## 8. 推荐使用方式

低频、大小不固定：

```c
void *p = osal_mem_alloc(size);
osal_mem_free(p);
```

高频、固定大小、实时路径：

```c
void *node = osal_mempool_alloc(pool);
osal_mempool_free(pool, node);
```

如果只使用 OSAL 的 `task/queue/timer`，通常直接依赖统一静态堆即可。只有业务层有严格固定块管理需求时，才单独创建 `osal_mempool`。
