#ifndef OSAL_TASK_H
#define OSAL_TASK_H

#include <stdbool.h>
#include "osal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 任务入口函数类型。
 * 调度器每次真正执行某个任务时，最终调用的就是这种函数。 */
typedef void (*osal_task_fn_t)(void *arg);

/* 任务控制块前向声明。
 * 用户只拿句柄，不直接访问内部成员。 */
typedef struct osal_task osal_task_t;

/*
 * 任务句柄使用约定
 * ---------------------------------------------------------------------------
 * 1. osal_task_create() 成功后，返回的句柄归调用方持有。
 * 2. osal_task_delete(NULL) 是安全空操作，便于用户少写判空分支。
 * 3. osal_task_delete() 成功后，句柄立即失效，不能再继续传给
 *    start / stop / sleep / sleep_until / delete。
 * 4. 如果 debug 打开，OSAL 能检测到的重复 delete、非法句柄、
 *    错误上下文使用会通过 OSAL_DEBUG_HOOK 上报。
 *
 * 接口能力矩阵
 * ---------------------------------------------------------------------------
 * 1. create / delete / start / stop / sleep / sleep_until / yield / run
 *    只能在任务态或主循环里使用。
 * 2. ISR 中不允许调用本头文件里的这些接口，因为它们可能修改任务链表、
 *    改变阻塞状态，或者需要重新进入调度器。
 */

/**
 * @brief 任务优先级。
 *
 * @note 这里的优先级不是“抢占优先级”，而是“调度检查顺序优先级”。
 *       也就是说：
 *       1. 高优先级任务每轮最先检查。
 *       2. 中优先级任务每轮在高优先级后检查。
 *       3. 低优先级任务不是每轮都检查，而是隔几轮补偿检查一次，
 *          或者当高/中优先级本轮都没有可运行任务时再检查。
 *
 * @note 这种设计适合裸机协作式框架：
 *       1. 没有抢占开销。
 *       2. 关键任务更容易先得到运行机会。
 *       3. 低优先级任务也不会被永久饿死。
 */
typedef enum {
    OSAL_TASK_PRIORITY_HIGH = 0,   /* 最先被检查的任务。 */
    OSAL_TASK_PRIORITY_MEDIUM = 1, /* 次一级任务。 */
    OSAL_TASK_PRIORITY_LOW = 2,    /* 延后检查的后台任务。 */
    OSAL_TASK_PRIORITY_COUNT
} osal_task_priority_t;

/**
 * @brief 任务当前所处的调度状态。
 *
 * @note 这几个状态由 OSAL 内部维护，用户通常只需要理解它们的语义：
 *       1. READY：任务可以被调度器执行。
 *       2. RUNNING：任务当前正在执行。
 *       3. BLOCKED：任务正在等待时间或等待对象，不参与普通调度。
 *       4. SUSPENDED：任务被手动停止，不参与调度。
 */
typedef enum {
    OSAL_TASK_READY = 0,
    OSAL_TASK_RUNNING,
    OSAL_TASK_BLOCKED,
    OSAL_TASK_SUSPENDED
} osal_task_state_t;

/**
 * @brief 任务当前阻塞的原因。
 *
 * @note 这个枚举主要给 OSAL 内部用，用来区分：
 *       1. 普通 sleep 挂起。
 *       2. 等待队列可写。
 *       3. 等待队列可读。
 *       4. 等待事件触发。
 *       5. 等待互斥量解锁。
 *
 * @note 之所以要把“等待原因”单独记录下来，是为了在任务被唤醒时知道：
 *       1. 它是被谁挂起的。
 *       2. 它现在应该由谁来恢复。
 *       3. 它恢复时带回来的状态码应不应该被本次 API 消费掉。
 */
typedef enum {
    OSAL_TASK_WAIT_NONE = 0,       /* 当前没有等待任何对象。 */
    OSAL_TASK_WAIT_SLEEP,          /* 正在等待时间到期。 */
    OSAL_TASK_WAIT_QUEUE_SEND,     /* 正在等待队列出现空位。 */
    OSAL_TASK_WAIT_QUEUE_RECV,     /* 正在等待队列出现消息。 */
    OSAL_TASK_WAIT_EVENT,          /* 正在等待事件对象被置位。 */
    OSAL_TASK_WAIT_MUTEX_LOCK      /* 正在等待互斥量变为可获取。 */
} osal_task_wait_reason_t;

/**
 * @brief 创建一个协作式任务对象。
 *
 * @param fn 任务入口函数。调度器真正执行任务时，会调用这个函数。
 * @param arg 传给 fn 的用户参数。
 * @param priority 任务调度优先级。
 *
 * @return 成功返回任务句柄，失败返回 NULL。
 *
 * @note create 只负责“创建任务对象”，不会自动启动任务。
 *       任务创建成功后，通常还需要再调用 osal_task_start()。
 *
 * @note 这里不会创建独立栈，也不会做上下文切换。
 *       这个 OSAL 是协作式模型，任务函数本质上仍运行在同一个线程上下文里。
 */
osal_task_t *osal_task_create(osal_task_fn_t fn, void *arg, osal_task_priority_t priority);

/**
 * @brief 销毁一个任务对象。
 *
 * @param task 任务句柄。
 *
 * @note 如果传入 NULL，函数直接返回。
 * @note 删除成功后，句柄立即失效。
 * @note 如果 debug 打开，重复 delete 或陈旧句柄会尽量给出诊断。
 */
void osal_task_delete(osal_task_t *task);

/**
 * @brief 启动一个已经创建好的任务。
 *
 * @param task 任务句柄。
 *
 * @return OSAL_OK 表示任务已被置为 READY；
 *         其他状态码表示句柄非法或当前状态不允许启动。
 *
 * @note 只有进入 READY 状态后，任务才会被 osal_run() 扫描到。
 */
osal_status_t osal_task_start(osal_task_t *task);

/**
 * @brief 停止一个任务。
 *
 * @param task 任务句柄。
 *
 * @return OSAL_OK 表示任务已进入 SUSPENDED；
 *         其他状态码表示操作失败。
 *
 * @note stop 不是 delete。
 *       stop 之后任务对象仍然存在，只是不再参与调度。
 */
osal_status_t osal_task_stop(osal_task_t *task);

/**
 * @brief 让任务相对休眠指定毫秒数。
 *
 * @param task 任务句柄。传 NULL 表示“当前正在运行的任务”。
 * @param ms 休眠时长，单位是毫秒。
 *
 * @return OSAL 状态码。
 *
 * @note 这不是忙等延时。
 *       它的真实含义是：
 *       1. 把任务状态改成 BLOCKED。
 *       2. 记录当前时间和超时时间。
 *       3. 让调度器在时间到达前跳过这个任务。
 *
 * @note 因为它是“相对休眠”，所以如果任务函数本身执行时间不固定，
 *       周期任务使用它时可能会慢慢积累偏差。
 */
osal_status_t osal_task_sleep(osal_task_t *task, uint32_t ms);

/**
 * @brief 让任务按固定周期休眠到下一次唤醒点。
 *
 * @param task 任务句柄。传 NULL 表示“当前正在运行的任务”。
 * @param period_ms 周期长度，单位是毫秒。
 *
 * @return OSAL 状态码。
 *
 * @note 这是更适合周期任务的接口，语义接近 FreeRTOS 的 vTaskDelayUntil()。
 *
 * @note 与 osal_task_sleep() 的区别：
 *       1. sleep() 是“从当前时刻开始，再睡 N 毫秒”。
 *       2. sleep_until() 是“按内部维护的周期锚点，睡到下一个固定节拍”。
 *
 * @note 当前实现里，周期锚点由任务控制块内部自动维护，
 *       调用方不需要额外准备静态变量。
 */
osal_status_t osal_task_sleep_until(osal_task_t *task, uint32_t period_ms);

/**
 * @brief 主动让出本轮执行机会。
 *
 * @note 这个接口通常用于：
 *       1. 任务希望更快让其他任务运行。
 *       2. 某些轻量轮询型逻辑不想占住整轮调度。
 *
 * @note yield 不会阻塞当前任务，只是尽快触发一次新的调度轮次。
 */
void osal_task_yield(void);

/**
 * @brief 执行一轮 OSAL 协作式调度。
 *
 * @note 这个接口通常放在主循环里持续调用：
 *       while (1) { osal_run(); }
 *
 * @note 它会做几件事：
 *       1. 轮询软件定时器。
 *       2. 检查高优先级任务。
 *       3. 检查中优先级任务。
 *       4. 按补偿策略检查低优先级任务。
 *
 * @note 它不是 RTOS 的调度中断，也不会切换栈。
 *       是否“好用”，很大程度上取决于任务函数是否短小且非阻塞。
 */
void osal_run(void);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_TASK_H */
