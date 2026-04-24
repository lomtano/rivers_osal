# mem：统一堆与内存池

## 1. 模块职责

`mem` 提供两类内存能力：

- 统一静态堆
- 基于用户缓冲区的定长内存池

当前 OSAL 的 `task`、`queue` 等对象，默认都依赖统一静态堆分配。

## 2. 统一静态堆

### 2.1 对外接口

- `osal_mem_init()`
- `osal_mem_alloc()`
- `osal_mem_free()`
- `osal_mem_get_free_size()`

### 2.2 使用规则

- `osal_mem_alloc()` 返回的块必须用 `osal_mem_free()` 归还
- `free(NULL)` 是安全空操作
- 如果没有主动调用 `osal_mem_init()`，模块会在首次分配时回退到内部默认静态堆

### 2.3 当前用途

当前这些对象都直接使用统一静态堆：

- 任务控制块
- 队列控制块
- 队列数据区
- 软件定时器控制块
- 组件内部需要的动态对象

## 3. 定长内存池

### 3.1 对外接口

- `osal_mempool_create()`
- `osal_mempool_delete()`
- `osal_mempool_alloc()`
- `osal_mempool_free()`

### 3.2 适用场景

内存池更适合：

- 对象大小固定
- 申请释放频繁
- 希望在 ISR 中做固定块申请/归还

### 3.3 约束

- `pool_buffer` 生命周期由调用方负责
- 归还时必须回到同一个内存池
- `osal_mempool_delete()` 只删除控制块，不会释放用户传入的底层缓冲区

## 4. 接口上下文边界

统一堆：

- `mem_init / mem_alloc / mem_free / mem_get_free_size`：任务态

内存池：

- `mempool_create / mempool_delete`：任务态
- `mempool_alloc / mempool_free`：任务态 / ISR

## 5. 调试行为

当 `OSAL_CFG_ENABLE_DEBUG` 打开时，模块会尽量报告这些可检测问题：

- 二次释放
- 非法指针
- 非法 mempool 句柄
- 错误归还到别的内存池

报告方式统一通过 `OSAL_DEBUG_HOOK`。

## 6. 与当前 OSAL 模型的关系

当前 `queue` 已经不再支持用户自带静态消息缓冲区创建方式，因此：

- 如果你只是使用 OSAL 队列，通常直接依赖统一静态堆即可
- 如果你的业务确实需要严格固定块管理，可以在应用层单独使用 `osal_mempool`
