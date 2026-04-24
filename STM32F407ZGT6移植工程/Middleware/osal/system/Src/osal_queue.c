#include "../Inc/osal_queue.h"

#if OSAL_CFG_ENABLE_QUEUE

#include "../Inc/osal_irq.h"
#include "../Inc/osal_mem.h"
#include "../Inc/osal_timer.h"
#include <stdbool.h>
#include <string.h>

/* 仅供 system 层内部把已知临界区边界送进 DWT profiling。 */
struct osal_queue {
    uint8_t *storage;
    uint32_t head;
    uint32_t tail;
    uint32_t length;
    uint32_t item_size;
    uint32_t count;
    struct osal_queue *next;
};

static osal_queue_t *s_queue_list = NULL;

/* 函数说明：输出队列模块调试诊断信息。 */
static void osal_queue_report(const char *message) {
    OSAL_DEBUG_REPORT("queue", message);
}

#if OSAL_CFG_ENABLE_DEBUG
/* 函数说明：在 debug 模式下检查队列句柄是否仍在活动链表中。 */
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

/* 函数说明：统一校验队列句柄是否合法。 */
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

/* 函数说明：计算队列底层存储区总字节数，并防止 length * item_size 溢出。 */
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

/* 函数说明：把新建队列挂入活动链表。 */
static void osal_queue_link(osal_queue_t *q) {
    q->next = s_queue_list;
    s_queue_list = q;
}

/* 函数说明：把队列从活动链表中摘除。 */
static bool osal_queue_unlink(osal_queue_t *q) {
    osal_queue_t *prev = NULL;
    osal_queue_t *current = s_queue_list;

    while (current != NULL) {
        if (current == q) {
            if (prev == NULL) {
                s_queue_list = current->next;
            } else {
                prev->next = current->next;
            }
            current->next = NULL;
            return true;
        }
        prev = current;
        current = current->next;
    }

    return false;
}

/* 函数说明：在已进入临界区的前提下，向循环队列尾部压入一个元素。 */
static osal_status_t osal_queue_enqueue_locked(osal_queue_t *q, const void *item) {
    if (q->count >= q->length) {
        return OSAL_ERR_RESOURCE;
    }

    /* 固定项大小队列直接整块 memcpy 到 tail 位置。 */
    memcpy(q->storage + (q->tail * q->item_size), item, q->item_size);
    /* tail 自然绕回，实现环形缓冲区。 */
    q->tail = (q->tail + 1U) % q->length;
    q->count++;
    return OSAL_OK;
}

/* 函数说明：在已进入临界区的前提下，从循环队列头部弹出一个元素。 */
static osal_status_t osal_queue_dequeue_locked(osal_queue_t *q, void *item) {
    if (q->count == 0U) {
        return OSAL_ERR_RESOURCE;
    }

    memcpy(item, q->storage + (q->head * q->item_size), q->item_size);
    /* head 同样按环形方式推进。 */
    q->head = (q->head + 1U) % q->length;
    q->count--;
    return OSAL_OK;
}

/* 函数说明：在任务态做一次“立即发送”尝试。 */
static osal_status_t osal_queue_try_send(osal_queue_t *q, const void *item) {
    uint32_t irq_state = osal_internal_critical_enter();
    osal_status_t status = osal_queue_enqueue_locked(q, item);
    osal_internal_critical_exit(irq_state);
    return status;
}

/* 函数说明：在任务态做一次“立即接收”尝试。 */
static osal_status_t osal_queue_try_recv(osal_queue_t *q, void *item) {
    uint32_t irq_state = osal_internal_critical_enter();
    osal_status_t status = osal_queue_dequeue_locked(q, item);
    osal_internal_critical_exit(irq_state);
    return status;
}

/* 函数说明：创建一个固定长度、固定项大小的循环队列。 */
osal_queue_t *osal_queue_create(uint32_t length, uint32_t item_size) {
    osal_queue_t *q;
    uint8_t *storage;
    uint32_t total_size;

    if (osal_irq_is_in_isr()) {
        osal_queue_report("create is not allowed in ISR context");
        return NULL;
    }
    if (!osal_queue_storage_size(length, item_size, &total_size)) {
        return NULL;
    }

    /* 数据区和控制块都走统一的 OSAL 堆，生命周期一起受队列句柄管理。 */
    storage = (uint8_t *)osal_mem_alloc(total_size);
    if (storage == NULL) {
        return NULL;
    }

    q = (osal_queue_t *)osal_mem_alloc((uint32_t)sizeof(osal_queue_t));
    if (q == NULL) {
        osal_mem_free(storage);
        return NULL;
    }

    q->storage = storage;
    q->head = 0U;
    q->tail = 0U;
    q->length = length;
    q->item_size = item_size;
    q->count = 0U;
    q->next = NULL;
    osal_queue_link(q);
    return q;
}

/* 函数说明：删除队列并释放其控制块和存储区。 */
void osal_queue_delete(osal_queue_t *q) {
    uint32_t irq_state;

    if (q == NULL) {
        return;
    }
    if (osal_irq_is_in_isr()) {
        osal_queue_report("delete is not allowed in ISR context");
        return;
    }

    irq_state = osal_internal_critical_enter();
    if (!osal_queue_unlink(q)) {
        osal_internal_critical_exit(irq_state);
        osal_queue_report("delete called with inactive queue handle");
        return;
    }
    osal_internal_critical_exit(irq_state);

    osal_mem_free(q->storage);
    osal_mem_free(q);
}

/* 函数说明：读取队列当前元素数量。 */
uint32_t osal_queue_get_count(const osal_queue_t *q) {
    uint32_t irq_state;
    uint32_t count;

    if (!osal_queue_validate_handle(q)) {
        return 0U;
    }

    irq_state = osal_internal_critical_enter();
    count = q->count;
    osal_internal_critical_exit(irq_state);
    return count;
}

/*
 * 函数说明：在任务态向队列发送一个元素。
 * timeout_ms > 0 时，这里不是挂起等待，而是在同步循环里反复重试；
 * 时间判断使用无符号差值 `(now - start)`，允许 32 位 tick 自然回绕。
 */
osal_status_t osal_queue_send(osal_queue_t *q, const void *item, uint32_t timeout_ms) {
    uint32_t start_ms;
    osal_status_t status;

    if ((!osal_queue_validate_handle(q)) || (item == NULL)) {
        return OSAL_ERR_PARAM;
    }
    if (osal_irq_is_in_isr()) {
        osal_queue_report("send is not allowed in ISR context");
        return OSAL_ERR_ISR;
    }

    status = osal_queue_try_send(q, item);
    if ((status != OSAL_ERR_RESOURCE) || (timeout_ms == 0U)) {
        return status;
    }

    start_ms = osal_timer_get_tick();
    while ((uint32_t)(osal_timer_get_tick() - start_ms) < timeout_ms) {
        /* 每次都重新尝试一次，期间不隐式 yield，也不保存任务上下文。 */
        status = osal_queue_try_send(q, item);
        if (status == OSAL_OK) {
            return OSAL_OK;
        }
        if (status != OSAL_ERR_RESOURCE) {
            return status;
        }
    }

    return OSAL_ERR_TIMEOUT;
}

/*
 * 函数说明：在任务态从队列接收一个元素。
 * 它和 send 一样采用同步忙等重试模型，不是阻塞式挂起队列。
 */
osal_status_t osal_queue_recv(osal_queue_t *q, void *item, uint32_t timeout_ms) {
    uint32_t start_ms;
    osal_status_t status;

    if ((!osal_queue_validate_handle(q)) || (item == NULL)) {
        return OSAL_ERR_PARAM;
    }
    if (osal_irq_is_in_isr()) {
        osal_queue_report("recv is not allowed in ISR context");
        return OSAL_ERR_ISR;
    }

    status = osal_queue_try_recv(q, item);
    if ((status != OSAL_ERR_RESOURCE) || (timeout_ms == 0U)) {
        return status;
    }

    start_ms = osal_timer_get_tick();
    while ((uint32_t)(osal_timer_get_tick() - start_ms) < timeout_ms) {
        /* 只有真正读到数据才返回 OK；其余资源不足场景继续重试到超时。 */
        status = osal_queue_try_recv(q, item);
        if (status == OSAL_OK) {
            return OSAL_OK;
        }
        if (status != OSAL_ERR_RESOURCE) {
            return status;
        }
    }

    return OSAL_ERR_TIMEOUT;
}

/* 函数说明：在 ISR 中立即向队列发送一个元素。 */
osal_status_t osal_queue_send_from_isr(osal_queue_t *q, const void *item) {
    uint32_t irq_state;
    osal_status_t status;

    if ((!osal_queue_validate_handle(q)) || (item == NULL)) {
        return OSAL_ERR_PARAM;
    }

    irq_state = osal_internal_critical_enter();
    status = osal_queue_enqueue_locked(q, item);
    osal_internal_critical_exit(irq_state);
    return status;
}

/* 函数说明：在 ISR 中立即从队列接收一个元素。 */
osal_status_t osal_queue_recv_from_isr(osal_queue_t *q, void *item) {
    uint32_t irq_state;
    osal_status_t status;

    if ((!osal_queue_validate_handle(q)) || (item == NULL)) {
        return OSAL_ERR_PARAM;
    }

    irq_state = osal_internal_critical_enter();
    status = osal_queue_dequeue_locked(q, item);
    osal_internal_critical_exit(irq_state);
    return status;
}

#endif /* OSAL_CFG_ENABLE_QUEUE */
