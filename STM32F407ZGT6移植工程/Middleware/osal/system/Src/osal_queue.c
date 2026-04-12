#include "../Inc/osal.h"

#if OSAL_CFG_ENABLE_QUEUE

#include "../Inc/osal_queue.h"
#include "../Inc/osal_mem.h"
#include <stdbool.h>
#include <string.h>

/*
 * 说明：
 * 1. 下面这组内部前置声明只给 system/Src 内部模块互相调用。
 * 2. queue.c 需要借助 task.c 提供的内部等待状态管理接口，但这些接口不对外公开。
 */
osal_task_t *osal_task_get_current_internal(void);
bool osal_task_contains_internal(osal_task_t *task);
osal_task_priority_t osal_task_get_priority_internal(const osal_task_t *task);
osal_task_t *osal_task_get_wait_next_internal(const osal_task_t *task);
void osal_task_set_wait_next_internal(osal_task_t *task, osal_task_t *next);
bool osal_task_is_waiting_internal(const osal_task_t *task,
                                   osal_task_wait_reason_t reason,
                                   const void *object);
osal_status_t osal_task_block_current_internal(osal_task_wait_reason_t reason,
                                               void *object,
                                               uint32_t timeout_ms);
void osal_task_wake_internal(osal_task_t *task, osal_status_t resume_status);
osal_status_t osal_task_consume_wait_result_internal(osal_task_t *task,
                                                     osal_task_wait_reason_t reason,
                                                     const void *object,
                                                     bool *handled);
void osal_queue_cancel_wait_internal(void *queue_object,
                                     osal_task_t *task,
                                     osal_task_wait_reason_t reason);

/*
 * 队列对象说明：
 * 1. storage 是实际消息缓冲区，队列项按固定 item_size 拷贝进出。
 * 2. head/tail/count/length 共同组成环形队列状态。
 * 3. owns_storage 表示 storage 是否由 OSAL 堆分配，删除队列时决定是否一起释放。
 * 4. wait_send_list / wait_recv_list 是两条等待链表：
 *    - 队列满时，发送任务挂到 wait_send_list
 *    - 队列空时，接收任务挂到 wait_recv_list
 */
struct osal_queue {
    uint8_t *storage;
    uint32_t head;
    uint32_t tail;
    uint32_t length;
    uint32_t item_size;
    uint32_t count;
    bool owns_storage;
    osal_task_t *wait_send_list;
    osal_task_t *wait_recv_list;
    struct osal_queue *next;
};

/* 活动队列链表，用于调试句柄校验和 delete 时查找对象。 */
static osal_queue_t *s_queue_list = NULL;

/* 函数说明：输出队列模块调试诊断信息。 */
static void osal_queue_report(const char *message) {
    OSAL_DEBUG_REPORT("queue", message);
}

/* 函数说明：检查队列句柄是否仍在活动链表中。 */
#if OSAL_CFG_ENABLE_DEBUG
static bool osal_queue_contains(osal_queue_t *q) {
    osal_queue_t *current = s_queue_list;

    while (current != NULL) {
        if (current == q) {
            return true;
        }
        current = current->next;
    }

    return false;
}
#endif

/* 函数说明：校验队列句柄是否有效。 */
static bool osal_queue_validate_handle(const osal_queue_t *q) {
    if (q == NULL) {
        return false;
    }
#if OSAL_CFG_ENABLE_DEBUG
    if (!osal_queue_contains((osal_queue_t *)q)) {
        /*
         * 这里只在 debug 模式下做“活动链表校验”。
         * release 模式下省掉这一步，可以减少一点运行时开销。
         */
        osal_queue_report("API called with inactive queue handle");
        return false;
    }
#endif
    return true;
}

/* 函数说明：计算队列数据区所需的总字节数。 */
static bool osal_queue_storage_size(uint32_t length, uint32_t item_size, uint32_t *total_size) {
    uint64_t bytes;

    if ((length == 0U) || (item_size == 0U) || (total_size == NULL)) {
        return false;
    }

    /* 先用 64 位乘法，避免 length * item_size 在 32 位上先溢出。 */
    bytes = (uint64_t)length * (uint64_t)item_size;
    if (bytes > 0xFFFFFFFFULL) {
        return false;
    }

    *total_size = (uint32_t)bytes;
    return true;
}

/* 函数说明：将队列对象挂入活动链表。 */
static void osal_queue_link(osal_queue_t *q) {
    /* 新建队列直接头插到活动链表，便于后续统一做句柄校验。 */
    q->next = s_queue_list;
    s_queue_list = q;
}

/* 函数说明：根据等待原因返回对应的等待链表头指针。 */
static osal_task_t **osal_queue_wait_list(osal_queue_t *q, osal_task_wait_reason_t reason) {
    if (q == NULL) {
        return NULL;
    }

    switch (reason) {
    case OSAL_TASK_WAIT_QUEUE_SEND:
        /* 队列满时，发送者会挂到这条链表上。 */
        return &q->wait_send_list;

    case OSAL_TASK_WAIT_QUEUE_RECV:
        /* 队列空时，接收者会挂到这条链表上。 */
        return &q->wait_recv_list;

    case OSAL_TASK_WAIT_NONE:
    case OSAL_TASK_WAIT_SLEEP:
    default:
        return NULL;
    }
}

/* 函数说明：判断任务是否已经挂在某条等待链表中。 */
static bool osal_queue_wait_list_contains(osal_task_t *head, osal_task_t *task) {
    while (head != NULL) {
        if (head == task) {
            return true;
        }
        /* 等待链表不用 next，而是单独走 wait_next。 */
        head = osal_task_get_wait_next_internal(head);
    }

    return false;
}

/* 函数说明：把任务追加到等待链表尾部，保持同优先级下的先到先服务。 */
static void osal_queue_wait_list_append(osal_task_t **head, osal_task_t *task) {
    osal_task_t *current;

    if ((head == NULL) || (task == NULL)) {
        return;
    }
    if (osal_queue_wait_list_contains(*head, task)) {
        /* 同一个任务不允许重复挂进同一条等待链表。 */
        return;
    }

    osal_task_set_wait_next_internal(task, NULL);
    if (*head == NULL) {
        /* 空等待链表时，当前任务直接成为第一个等待者。 */
        *head = task;
        return;
    }

    current = *head;
    while (osal_task_get_wait_next_internal(current) != NULL) {
        /* 走到尾部追加，保证同优先级下保持先等待先服务。 */
        current = osal_task_get_wait_next_internal(current);
    }
    osal_task_set_wait_next_internal(current, task);
}

/* 函数说明：把指定任务从等待链表中移除。 */
static void osal_queue_wait_list_remove(osal_task_t **head, osal_task_t *task) {
    osal_task_t *prev = NULL;
    osal_task_t *current;

    if ((head == NULL) || (task == NULL)) {
        return;
    }

    current = *head;
    while (current != NULL) {
        osal_task_t *next = osal_task_get_wait_next_internal(current);

        if (current == task) {
            if (prev == NULL) {
                /* 删除的是头节点时，头指针直接后移。 */
                *head = next;
            } else {
                /* 删除的是中间节点时，让前驱节点跳过当前节点。 */
                osal_task_set_wait_next_internal(prev, next);
            }
            /* 被摘下来的任务不应再保留旧的等待链表链接。 */
            osal_task_set_wait_next_internal(current, NULL);
            return;
        }

        prev = current;
        current = next;
    }
}

/*
 * 说明：
 * 1. 这里只在发现“严格更高优先级”时才替换 best。
 * 2. 因此同优先级任务会保持先进入等待链表的任务先被唤醒，即 FIFO。
 */
/* 函数说明：从等待链表中弹出“最高优先级、同级先入先出”的任务。 */
static osal_task_t *osal_queue_wait_list_pop_best(osal_task_t **head) {
    osal_task_t *best = NULL;
    osal_task_t *best_prev = NULL;
    osal_task_t *prev = NULL;
    osal_task_t *current;

    if ((head == NULL) || (*head == NULL)) {
        return NULL;
    }

    current = *head;
    while (current != NULL) {
        /* 数值越小表示优先级越高，所以这里用“小于”来选更高优先级。 */
        if ((best == NULL) ||
            (osal_task_get_priority_internal(current) < osal_task_get_priority_internal(best))) {
            best = current;
            best_prev = prev;
        }
        prev = current;
        current = osal_task_get_wait_next_internal(current);
    }

    if (best == NULL) {
        return NULL;
    }

    if (best_prev == NULL) {
        /* 如果最佳任务就是链表头，直接把头指针后移。 */
        *head = osal_task_get_wait_next_internal(best);
    } else {
        /* 否则跳过 best，把它从等待链表中摘掉。 */
        osal_task_set_wait_next_internal(best_prev, osal_task_get_wait_next_internal(best));
    }
    /* 被弹出的任务已经脱离等待链表，所以 wait_next 必须清空。 */
    osal_task_set_wait_next_internal(best, NULL);
    return best;
}

/* 函数说明：唤醒一个等待当前队列事件的任务。 */
static void osal_queue_wake_one_waiter_locked(osal_queue_t *q, osal_task_wait_reason_t reason) {
    osal_task_t **wait_list = osal_queue_wait_list(q, reason);
    osal_task_t *task;

    if (wait_list == NULL) {
        return;
    }

    task = osal_queue_wait_list_pop_best(wait_list);
    if (task != NULL) {
        /* 被唤醒后会回到 READY，下一轮调度器就有机会继续运行它。 */
        osal_task_wake_internal(task, OSAL_OK);
    }
}

/* 函数说明：批量唤醒等待链表上的所有任务。 */
static void osal_queue_wake_all_waiters_locked(osal_queue_t *q,
                                               osal_task_wait_reason_t reason,
                                               osal_status_t resume_status) {
    osal_task_t **wait_list = osal_queue_wait_list(q, reason);

    if (wait_list == NULL) {
        return;
    }

    while (*wait_list != NULL) {
        osal_task_t *task = osal_queue_wait_list_pop_best(wait_list);

        if (task == NULL) {
            break;
        }
        /* delete 队列时常用这个路径，把所有等待者统一带错误码唤醒。 */
        osal_task_wake_internal(task, resume_status);
    }
}

/*
 * 环形队列写入说明：
 * 1. 按 tail 定位当前写入槽位。
 * 2. 写完后 tail 前进一格。
 * 3. 到达末尾后通过取模回卷到 0。
 */
/* 函数说明：把一项消息写入队列尾部。 */
static osal_status_t osal_queue_enqueue_locked(osal_queue_t *q, const void *item) {
    if (q->count >= q->length) {
        /* count 已经到达容量上限，说明队列满。 */
        return OSAL_ERR_RESOURCE;
    }

    /* 发送语义是“把一整项消息按 item_size 原样拷贝进队列”。 */
    memcpy(q->storage + (q->tail * q->item_size), item, q->item_size);
    /* tail 永远指向“下一个可写槽位”。 */
    q->tail = (q->tail + 1U) % q->length;
    /* 真实队列项数量加一。 */
    q->count++;
    return OSAL_OK;
}

/*
 * 环形队列读取说明：
 * 1. 按 head 定位当前读取槽位。
 * 2. 读完后 head 前进一格。
 * 3. 到达末尾后同样通过取模回卷到 0。
 */
/* 函数说明：从队列头部读出一项消息。 */
static osal_status_t osal_queue_dequeue_locked(osal_queue_t *q, void *item) {
    if (q->count == 0U) {
        /* 没有消息可读时，直接返回资源不可用。 */
        return OSAL_ERR_RESOURCE;
    }

    /* 接收语义同样是整项拷贝。 */
    memcpy(item, q->storage + (q->head * q->item_size), q->item_size);
    /* head 永远指向“下一项可读消息”。 */
    q->head = (q->head + 1U) % q->length;
    /* 真实队列项数量减一。 */
    q->count--;
    return OSAL_OK;
}

/* 函数说明：发送成功后顺带唤醒一个等消息的接收任务。 */
static osal_status_t osal_queue_send_locked(osal_queue_t *q, const void *item) {
    osal_status_t status = osal_queue_enqueue_locked(q, item);

    if (status == OSAL_OK) {
        /* 发送成功后，队列里出现了新消息，因此可以唤醒一个“等接收”的任务。 */
        osal_queue_wake_one_waiter_locked(q, OSAL_TASK_WAIT_QUEUE_RECV);
    }

    return status;
}

/* 函数说明：接收成功后顺带唤醒一个等空位的发送任务。 */
static osal_status_t osal_queue_recv_locked(osal_queue_t *q, void *item) {
    osal_status_t status = osal_queue_dequeue_locked(q, item);

    if (status == OSAL_OK) {
        /* 接收成功后，队列空出了一个槽位，因此可以唤醒一个“等发送”的任务。 */
        osal_queue_wake_one_waiter_locked(q, OSAL_TASK_WAIT_QUEUE_SEND);
    }

    return status;
}

/*
 * 说明：
 * 1. 这里返回 OSAL_ERR_BLOCKED 并不表示“最终失败”。
 * 2. 它表示“当前任务已经被挂起，本次 API 调用先结束，等任务被唤醒后再重新进入同一个 API”。
 * 3. 唤醒后，真正的恢复结果会由 osal_queue_consume_wait_result() 消费。
 */
/* 函数说明：把当前任务挂到指定队列等待链表中。 */
static osal_status_t osal_queue_prepare_wait_locked(osal_queue_t *q,
                                                    osal_task_wait_reason_t reason,
                                                    uint32_t timeout_ms) {
    osal_task_t *task = osal_task_get_current_internal();
    osal_task_t **wait_list = osal_queue_wait_list(q, reason);
    osal_status_t status;

    if ((task == NULL) || (wait_list == NULL)) {
        osal_queue_report("blocking queue wait requires current task context");
        return OSAL_ERROR;
    }
    if (!osal_task_contains_internal(task)) {
        osal_queue_report("blocking queue wait called with inactive task");
        return OSAL_ERR_PARAM;
    }
    if (osal_task_is_waiting_internal(task, reason, q)) {
        /*
         * 任务已经因为同一个队列、同一种原因挂起过一次了。
         * 这里直接返回“资源仍不可用”，避免重复插入等待链表。
         */
        return OSAL_ERR_BLOCKED;
    }

    status = osal_task_block_current_internal(reason, q, timeout_ms);
    if (status != OSAL_OK) {
        return status;
    }

    /* 任务控制块已经进入 BLOCKED，这里再把它真正挂进队列的等待链表。 */
    osal_queue_wait_list_append(wait_list, task);
    return OSAL_ERR_BLOCKED;
}

/*
 * 说明：
 * 1. 任务从 BLOCKED 恢复后，会再次进入 send_timeout/recv_timeout。
 * 2. 这时真正的恢复结果（正常唤醒、超时、队列删除）已经写在任务控制块里。
 * 3. 这里负责把它取出来，并转换成当前 API 需要返回给调用者的状态码。
 */
/* 函数说明：尝试消费一次队列等待恢复结果，例如超时返回。 */
static osal_status_t osal_queue_consume_wait_result(osal_queue_t *q,
                                                    osal_task_wait_reason_t reason,
                                                    bool *handled) {
    /* queue.c 自己不保存恢复结果，统一委托 task.c 从当前任务控制块里取。 */
    return osal_task_consume_wait_result_internal(NULL, reason, q, handled);
}

/* 函数说明：使用给定存储区创建一个内部队列对象。 */
static osal_queue_t *osal_queue_create_internal(uint8_t *storage,
                                                uint32_t length,
                                                uint32_t item_size,
                                                bool owns_storage) {
    osal_queue_t *q;

    if (storage == NULL) {
        return NULL;
    }

    q = (osal_queue_t *)osal_mem_alloc((uint32_t)sizeof(osal_queue_t));
    if (q == NULL) {
        if (owns_storage) {
            /* 如果 storage 也是本函数内部申请的，失败时要一起回收。 */
            osal_mem_free(storage);
        }
        return NULL;
    }

    q->storage = storage;
    q->head = 0U;
    q->tail = 0U;
    q->length = length;
    q->item_size = item_size;
    q->count = 0U;
    q->owns_storage = owns_storage;
    q->wait_send_list = NULL;
    q->wait_recv_list = NULL;
    /* 完整初始化后再挂入活动链表，避免半初始化对象被外部看到。 */
    osal_queue_link(q);
    return q;
}

/* 函数说明：创建一个使用 OSAL 堆存储的队列对象。 */
osal_queue_t *osal_queue_create(uint32_t length, uint32_t item_size) {
    uint32_t total_size;
    uint8_t *storage;

    if (osal_irq_is_in_isr()) {
        osal_queue_report("create is not allowed in ISR context");
        return NULL;
    }
    if (!osal_queue_storage_size(length, item_size, &total_size)) {
        return NULL;
    }

    storage = (uint8_t *)osal_mem_alloc(total_size);
    if (storage == NULL) {
        return NULL;
    }

    /* 动态模式下，消息缓冲区和控制块都来自 OSAL 统一堆。 */
    return osal_queue_create_internal(storage, length, item_size, true);
}

/* 函数说明：使用用户提供缓冲区创建一个静态队列对象。 */
osal_queue_t *osal_queue_create_static(void *buffer, uint32_t length, uint32_t item_size) {
    uint32_t total_size;

    if (osal_irq_is_in_isr()) {
        osal_queue_report("create_static is not allowed in ISR context");
        return NULL;
    }
    if ((buffer == NULL) || !osal_queue_storage_size(length, item_size, &total_size)) {
        return NULL;
    }

    (void)total_size;
    /* 静态模式下，用户自己提供消息缓冲区，控制块仍由 OSAL 统一分配。 */
    return osal_queue_create_internal((uint8_t *)buffer, length, item_size, false);
}

/* 函数说明：删除一个队列对象。 */
void osal_queue_delete(osal_queue_t *q) {
    osal_queue_t *prev = NULL;
    osal_queue_t *current = s_queue_list;
    uint32_t irq_state;

    if (q == NULL) {
        return;
    }
    if (osal_irq_is_in_isr()) {
        osal_queue_report("delete is not allowed in ISR context");
        return;
    }

    irq_state = osal_irq_disable();
    while (current != NULL) {
        if (current == q) {
            if (prev == NULL) {
                s_queue_list = current->next;
            } else {
                prev->next = current->next;
            }

            /* 删除队列前先唤醒所有等待它的任务，明确告诉它们“等待对象已经被删除”。 */
            osal_queue_wake_all_waiters_locked(current, OSAL_TASK_WAIT_QUEUE_SEND, OSAL_ERR_DELETED);
            osal_queue_wake_all_waiters_locked(current, OSAL_TASK_WAIT_QUEUE_RECV, OSAL_ERR_DELETED);
            osal_irq_restore(irq_state);

            if (current->owns_storage) {
                /* 只有 create() 模式下的数据区来自 OSAL 堆，才需要一起释放。 */
                osal_mem_free(current->storage);
            }
            /* 最后释放队列控制块本身。 */
            osal_mem_free(current);
            return;
        }
        prev = current;
        current = current->next;
    }
    osal_irq_restore(irq_state);

    osal_queue_report("delete called with inactive queue handle");
}

/* 函数说明：获取当前队列中的消息数量。 */
uint32_t osal_queue_get_count(const osal_queue_t *q) {
    uint32_t irq_state;
    uint32_t count;

    if (!osal_queue_validate_handle(q)) {
        return 0U;
    }

    irq_state = osal_irq_disable();
    /* count 是共享状态，读它时也要进临界区，避免与 send/recv 并发冲突。 */
    count = q->count;
    osal_irq_restore(irq_state);
    return count;
}

/* 函数说明：以非阻塞方式向队列发送一项消息。 */
osal_status_t osal_queue_send(osal_queue_t *q, const void *item) {
    uint32_t irq_state;
    osal_status_t status;

    if ((!osal_queue_validate_handle(q)) || (item == NULL)) {
        return OSAL_ERR_PARAM;
    }

    irq_state = osal_irq_disable();
    /* 非阻塞 send 的语义很简单：成功就发，满了就立刻返回。 */
    status = osal_queue_send_locked(q, item);
    osal_irq_restore(irq_state);
    return status;
}

/* 函数说明：以非阻塞方式从队列接收一项消息。 */
osal_status_t osal_queue_recv(osal_queue_t *q, void *item) {
    uint32_t irq_state;
    osal_status_t status;

    if ((!osal_queue_validate_handle(q)) || (item == NULL)) {
        return OSAL_ERR_PARAM;
    }

    irq_state = osal_irq_disable();
    /* 非阻塞 recv：有消息就取，没有就立刻返回。 */
    status = osal_queue_recv_locked(q, item);
    osal_irq_restore(irq_state);
    return status;
}

/* 函数说明：在指定等待策略下向队列发送一项消息。 */
osal_status_t osal_queue_send_timeout(osal_queue_t *q, const void *item, uint32_t timeout_ms) {
    uint32_t irq_state;
    osal_status_t status;
    bool handled = false;

    if ((!osal_queue_validate_handle(q)) || (item == NULL)) {
        return OSAL_ERR_PARAM;
    }
    if (osal_irq_is_in_isr()) {
        osal_queue_report("send_timeout is not allowed in ISR context");
        return OSAL_ERR_ISR;
    }

    status = osal_queue_consume_wait_result(q, OSAL_TASK_WAIT_QUEUE_SEND, &handled);
    if (handled) {
        /* 这里说明本次是“从上一次阻塞等待中恢复后再次进入 API”。 */
        return status;
    }

    irq_state = osal_irq_disable();
    /* 先尝试立即发送一次，避免明明队列已有空位还白白进入等待。 */
    status = osal_queue_send_locked(q, item);
    if (status == OSAL_OK) {
        osal_irq_restore(irq_state);
        return OSAL_OK;
    }
    if ((status != OSAL_ERR_RESOURCE) || (timeout_ms == 0U)) {
        /* 不是“队列满”或者用户要求“不等待”时，都直接返回。 */
        osal_irq_restore(irq_state);
        return status;
    }

    /* 只有在“队列满且允许等待”时，才把当前任务挂到等待链表。 */
    status = osal_queue_prepare_wait_locked(q, OSAL_TASK_WAIT_QUEUE_SEND, timeout_ms);
    osal_irq_restore(irq_state);
    /*
     * 返回 OSAL_ERR_BLOCKED 的真正含义是：
     * 当前任务已经进入 BLOCKED，等待未来某个发送机会，而不是立刻失败。
     */
    return status;
}

/* 函数说明：在指定等待策略下从队列接收一项消息。 */
osal_status_t osal_queue_recv_timeout(osal_queue_t *q, void *item, uint32_t timeout_ms) {
    uint32_t irq_state;
    osal_status_t status;
    bool handled = false;

    if ((!osal_queue_validate_handle(q)) || (item == NULL)) {
        return OSAL_ERR_PARAM;
    }
    if (osal_irq_is_in_isr()) {
        osal_queue_report("recv_timeout is not allowed in ISR context");
        return OSAL_ERR_ISR;
    }

    status = osal_queue_consume_wait_result(q, OSAL_TASK_WAIT_QUEUE_RECV, &handled);
    if (handled) {
        /* 这里说明本次是“从上一次阻塞等待中恢复后再次进入 API”。 */
        return status;
    }

    irq_state = osal_irq_disable();
    /* 先尝试立即接收一次，避免明明已有消息还多挂起一轮。 */
    status = osal_queue_recv_locked(q, item);
    if (status == OSAL_OK) {
        osal_irq_restore(irq_state);
        return OSAL_OK;
    }
    if ((status != OSAL_ERR_RESOURCE) || (timeout_ms == 0U)) {
        /* 不是“队列空”或者用户要求“不等待”时，都直接返回。 */
        osal_irq_restore(irq_state);
        return status;
    }

    /* 只有在“队列空且允许等待”时，才把当前任务挂到等待链表。 */
    status = osal_queue_prepare_wait_locked(q, OSAL_TASK_WAIT_QUEUE_RECV, timeout_ms);
    osal_irq_restore(irq_state);
    /*
     * 返回 OSAL_ERR_BLOCKED 的含义同 send_timeout：
     * 当前任务已经挂起等待，不代表这次等待最终失败。
     */
    return status;
}

/* 函数说明：在中断上下文中向队列发送一项消息。 */
osal_status_t osal_queue_send_from_isr(osal_queue_t *q, const void *item) {
    if ((!osal_queue_validate_handle(q)) || (item == NULL)) {
        return OSAL_ERR_PARAM;
    }

    /* ISR 版本不做阻塞等待，只允许“能发就发，不能发就返回”。 */
    return osal_queue_send_locked(q, item);
}

/* 函数说明：在中断上下文中从队列接收一项消息。 */
osal_status_t osal_queue_recv_from_isr(osal_queue_t *q, void *item) {
    if ((!osal_queue_validate_handle(q)) || (item == NULL)) {
        return OSAL_ERR_PARAM;
    }

    /* ISR 版本同样不允许阻塞，只能立即尝试接收。 */
    return osal_queue_recv_locked(q, item);
}

/* 函数说明：把指定任务从某个队列的等待链表中移除。 */
void osal_queue_cancel_wait_internal(void *queue_object,
                                     osal_task_t *task,
                                     osal_task_wait_reason_t reason) {
    osal_queue_t *q = (osal_queue_t *)queue_object;
    osal_task_t **wait_list = osal_queue_wait_list(q, reason);

    if ((q == NULL) || (task == NULL) || (wait_list == NULL)) {
        return;
    }

    /* task.c 在 stop/delete/timeout 等路径里会调用这里，把任务从队列等待链表摘掉。 */
    osal_queue_wait_list_remove(wait_list, task);
}

#endif /* OSAL_CFG_ENABLE_QUEUE */




