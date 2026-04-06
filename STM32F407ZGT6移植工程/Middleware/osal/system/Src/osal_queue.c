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

static osal_queue_t *s_queue_list = NULL;

/* 函数说明：输出队列模块调试诊断信息。 */
static void osal_queue_report(const char *message) {
    OSAL_DEBUG_REPORT("queue", message);
}

/* 函数说明：检查队列句柄是否仍在活动链表中。 */
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

/* 函数说明：校验队列句柄是否有效。 */
static bool osal_queue_validate_handle(const osal_queue_t *q) {
    if (q == NULL) {
        return false;
    }
#if OSAL_CFG_ENABLE_DEBUG
    if (!osal_queue_contains((osal_queue_t *)q)) {
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

    bytes = (uint64_t)length * (uint64_t)item_size;
    if (bytes > 0xFFFFFFFFULL) {
        return false;
    }

    *total_size = (uint32_t)bytes;
    return true;
}

/* 函数说明：将队列对象挂入活动链表。 */
static void osal_queue_link(osal_queue_t *q) {
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
        return &q->wait_send_list;

    case OSAL_TASK_WAIT_QUEUE_RECV:
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
        return;
    }

    osal_task_set_wait_next_internal(task, NULL);
    if (*head == NULL) {
        *head = task;
        return;
    }

    current = *head;
    while (osal_task_get_wait_next_internal(current) != NULL) {
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
                *head = next;
            } else {
                osal_task_set_wait_next_internal(prev, next);
            }
            osal_task_set_wait_next_internal(current, NULL);
            return;
        }

        prev = current;
        current = next;
    }
}

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
        *head = osal_task_get_wait_next_internal(best);
    } else {
        osal_task_set_wait_next_internal(best_prev, osal_task_get_wait_next_internal(best));
    }
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
        osal_task_wake_internal(task, resume_status);
    }
}

/* 函数说明：把一项消息写入队列尾部。 */
static osal_status_t osal_queue_enqueue_locked(osal_queue_t *q, const void *item) {
    if (q->count >= q->length) {
        return OSAL_ERR_RESOURCE;
    }

    memcpy(q->storage + (q->tail * q->item_size), item, q->item_size);
    q->tail = (q->tail + 1U) % q->length;
    q->count++;
    return OSAL_OK;
}

/* 函数说明：从队列头部读出一项消息。 */
static osal_status_t osal_queue_dequeue_locked(osal_queue_t *q, void *item) {
    if (q->count == 0U) {
        return OSAL_ERR_RESOURCE;
    }

    memcpy(item, q->storage + (q->head * q->item_size), q->item_size);
    q->head = (q->head + 1U) % q->length;
    q->count--;
    return OSAL_OK;
}

/* 函数说明：发送成功后顺带唤醒一个等消息的接收任务。 */
static osal_status_t osal_queue_send_locked(osal_queue_t *q, const void *item) {
    osal_status_t status = osal_queue_enqueue_locked(q, item);

    if (status == OSAL_OK) {
        osal_queue_wake_one_waiter_locked(q, OSAL_TASK_WAIT_QUEUE_RECV);
    }

    return status;
}

/* 函数说明：接收成功后顺带唤醒一个等空位的发送任务。 */
static osal_status_t osal_queue_recv_locked(osal_queue_t *q, void *item) {
    osal_status_t status = osal_queue_dequeue_locked(q, item);

    if (status == OSAL_OK) {
        osal_queue_wake_one_waiter_locked(q, OSAL_TASK_WAIT_QUEUE_SEND);
    }

    return status;
}

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
        return OSAL_ERR_RESOURCE;
    }

    status = osal_task_block_current_internal(reason, q, timeout_ms);
    if (status != OSAL_OK) {
        return status;
    }

    osal_queue_wait_list_append(wait_list, task);
    return OSAL_ERR_RESOURCE;
}

/* 函数说明：尝试消费一次队列等待恢复结果，例如超时返回。 */
static osal_status_t osal_queue_consume_wait_result(osal_queue_t *q,
                                                    osal_task_wait_reason_t reason,
                                                    bool *handled) {
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

            osal_queue_wake_all_waiters_locked(current, OSAL_TASK_WAIT_QUEUE_SEND, OSAL_ERR_RESOURCE);
            osal_queue_wake_all_waiters_locked(current, OSAL_TASK_WAIT_QUEUE_RECV, OSAL_ERR_RESOURCE);
            osal_irq_restore(irq_state);

            if (current->owns_storage) {
                osal_mem_free(current->storage);
            }
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
        return status;
    }

    irq_state = osal_irq_disable();
    status = osal_queue_send_locked(q, item);
    if (status == OSAL_OK) {
        osal_irq_restore(irq_state);
        return OSAL_OK;
    }
    if ((status != OSAL_ERR_RESOURCE) || (timeout_ms == 0U)) {
        osal_irq_restore(irq_state);
        return status;
    }

    status = osal_queue_prepare_wait_locked(q, OSAL_TASK_WAIT_QUEUE_SEND, timeout_ms);
    osal_irq_restore(irq_state);
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
        return status;
    }

    irq_state = osal_irq_disable();
    status = osal_queue_recv_locked(q, item);
    if (status == OSAL_OK) {
        osal_irq_restore(irq_state);
        return OSAL_OK;
    }
    if ((status != OSAL_ERR_RESOURCE) || (timeout_ms == 0U)) {
        osal_irq_restore(irq_state);
        return status;
    }

    status = osal_queue_prepare_wait_locked(q, OSAL_TASK_WAIT_QUEUE_RECV, timeout_ms);
    osal_irq_restore(irq_state);
    return status;
}

/* 函数说明：在中断上下文中向队列发送一项消息。 */
osal_status_t osal_queue_send_from_isr(osal_queue_t *q, const void *item) {
    if ((!osal_queue_validate_handle(q)) || (item == NULL)) {
        return OSAL_ERR_PARAM;
    }

    return osal_queue_send_locked(q, item);
}

/* 函数说明：在中断上下文中从队列接收一项消息。 */
osal_status_t osal_queue_recv_from_isr(osal_queue_t *q, void *item) {
    if ((!osal_queue_validate_handle(q)) || (item == NULL)) {
        return OSAL_ERR_PARAM;
    }

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

    osal_queue_wait_list_remove(wait_list, task);
}

#endif /* OSAL_CFG_ENABLE_QUEUE */
