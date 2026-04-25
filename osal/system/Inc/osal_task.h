#ifndef OSAL_TASK_H
#define OSAL_TASK_H

#include <stdbool.h>
#include "osal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*osal_task_fn_t)(void *arg);
typedef struct osal_task osal_task_t;

/*
 * 任务句柄使用约定：
 * 1. create 成功后句柄归调用方持有。
 * 2. delete(NULL) 是安全空操作。
 * 3. delete 成功后句柄立即失效，不能再继续 start/stop/delete。
 * 4. 当前 task 模块不提供任务阻塞/恢复抽象，只负责协作式调度。
 * 5. 运行中的任务不能在当前执行轮次里直接 delete，应等它 return 后再删除。
 * 6. 调度器正在扫描任务链表时不允许 delete 任意任务；需要删除时应先 stop，
 *    再在非调度回调阶段统一回收。
 */

typedef enum {
    OSAL_TASK_PRIORITY_HIGH = 0,
    OSAL_TASK_PRIORITY_MEDIUM = 1,
    OSAL_TASK_PRIORITY_LOW = 2,
    OSAL_TASK_PRIORITY_COUNT
} osal_task_priority_t;

typedef enum {
    OSAL_TASK_READY = 0,
    OSAL_TASK_RUNNING,
    OSAL_TASK_SUSPENDED
} osal_task_state_t;

/**
 * @brief 创建一个协作式任务控制块。
 * @param fn 每轮调度时被调用的任务函数。
 * @param arg 传递给任务函数的用户参数。
 * @param priority 任务优先级。
 * @return 成功返回任务句柄，失败返回 NULL。
 * @note 任务创建后初始处于 `OSAL_TASK_SUSPENDED` 状态，需要显式 start。
 */
osal_task_t *osal_task_create(osal_task_fn_t fn, void *arg, osal_task_priority_t priority);

/**
 * @brief 删除一个任务控制块。
 * @param task 任务句柄。
 * @note delete(NULL) 是安全空操作。
 * @note 当前正在执行的任务不能在本轮调用栈里直接删除，必须等它返回。
 * @note 调度器正在执行任务回调时不允许删除任意任务，避免链表遍历指针悬空。
 */
void osal_task_delete(osal_task_t *task);

/**
 * @brief 把任务切换到 ready 状态，允许调度器执行它。
 * @param task 任务句柄。
 * @return 成功返回 OSAL_OK。
 */
osal_status_t osal_task_start(osal_task_t *task);

/**
 * @brief 把任务切换到 suspended 状态，阻止后续调度。
 * @param task 任务句柄。
 * @return 成功返回 OSAL_OK。
 */
osal_status_t osal_task_stop(osal_task_t *task);

/*
 * yield 的语义：
 * 1. 在当前任务函数调用栈中，同步触发一次嵌套调度。
 * 2. 本轮嵌套调度不会立刻再次执行当前任务。
 * 3. 嵌套调度返回后，当前任务从 yield() 下一条语句继续执行。
 */
void osal_task_yield(void);

/*
 * start_system 的语义：
 * 1. 由应用在完成硬件初始化、osal_init() 和任务创建后调用。
 * 2. 调用后由 OSAL 接管顶层循环，持续推进软件定时器和协作式任务调度。
 * 3. 每轮调度结束后会调用 OSAL_IDLE_HOOK()，默认空操作，可用于接入低功耗。
 * 4. 正常情况下不会再返回到 main()，因此 main() 里不需要再写自己的顶层调度 while(1)。
 */
void osal_start_system(void);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_TASK_H */
