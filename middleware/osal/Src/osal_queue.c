/*
 * osal_queue.c
 * Ring buffer queue for message passing.
 * - Non-blocking and timeout send/receive
 * - ISR-safe operations with osal_irq_disable/restore
 */

#include "../Inc/osal_queue.h"
#include "../Inc/osal_mem.h"
#include "../Inc/osal_task.h"
#include <string.h>

struct osal_queue {
    uint8_t *buf;
    uint32_t head;
    uint32_t tail;
    uint32_t len;
    uint32_t item_size;
    uint32_t count;
    struct osal_queue *next;
};

static osal_queue_t *s_queue_list = NULL;

/* Allocate a queue control block from the unified OSAL heap. */
osal_queue_t *osal_queue_create(void *buffer, uint32_t length, uint32_t item_size) {
    osal_queue_t *q;

    if (!buffer || length == 0 || item_size == 0) return NULL;
    q = (osal_queue_t *)osal_mem_alloc((uint32_t)sizeof(osal_queue_t));
    if (q == NULL) return NULL;

    q->buf = (uint8_t *)buffer;
    q->head = 0U;
    q->tail = 0U;
    q->len = length;
    q->item_size = item_size;
    q->count = 0U;
    q->next = s_queue_list;
    s_queue_list = q;
    return q;
}

/* Remove a queue control block from the internal list and free it. */
void osal_queue_delete(osal_queue_t *q) {
    osal_queue_t *prev = NULL;
    osal_queue_t *current = s_queue_list;

    if (!q) return;
    while (current != NULL) {
        if (current == q) {
            if (prev == NULL) {
                s_queue_list = current->next;
            } else {
                prev->next = current->next;
            }
            osal_mem_free(current);
            return;
        }
        prev = current;
        current = current->next;
    }
}

/* Enqueue one item without entering or leaving a critical section. */
static osal_status_t _enqueue(osal_queue_t *q, const void *item) {
    if (q->count >= q->len) return OSAL_ERR_RESOURCE;
    memcpy(q->buf + q->tail * q->item_size, item, q->item_size);
    q->tail = (q->tail + 1) % q->len;
    q->count++;
    return OSAL_OK;
}

/* Dequeue one item without entering or leaving a critical section. */
static osal_status_t _dequeue(osal_queue_t *q, void *item) {
    if (q->count == 0) return OSAL_ERR_RESOURCE;
    memcpy(item, q->buf + q->head * q->item_size, q->item_size);
    q->head = (q->head + 1) % q->len;
    q->count--;
    return OSAL_OK;
}

/* Send one queue item with interrupt protection. */
osal_status_t osal_queue_send(osal_queue_t *q, const void *item) {
    if (!q || !item) return OSAL_ERR_PARAM;
    uint32_t irq_state = osal_irq_disable();
    osal_status_t res = _enqueue(q, item);
    osal_irq_restore(irq_state);
    return res;
}

/* Receive one queue item with interrupt protection. */
osal_status_t osal_queue_recv(osal_queue_t *q, void *item) {
    if (!q || !item) return OSAL_ERR_PARAM;
    uint32_t irq_state = osal_irq_disable();
    osal_status_t res = _dequeue(q, item);
    osal_irq_restore(irq_state);
    return res;
}

/* Retry queue send until success or timeout in task context. */
osal_status_t osal_queue_send_timeout(osal_queue_t *q, const void *item, uint32_t timeout_ms) {
    if (!q || !item) return OSAL_ERR_PARAM;
    if (osal_irq_is_in_isr()) return OSAL_ERR_ISR;
    uint32_t start = osal_timer_get_uptime_ms();
    while (1) {
        osal_status_t res = osal_queue_send(q, item);
        if (res == OSAL_OK) return OSAL_OK;
        if ((uint32_t)(osal_timer_get_uptime_ms() - start) >= timeout_ms) return OSAL_ERR_TIMEOUT;
        osal_task_yield();
    }
}

/* Retry queue receive until success or timeout in task context. */
osal_status_t osal_queue_recv_timeout(osal_queue_t *q, void *item, uint32_t timeout_ms) {
    if (!q || !item) return OSAL_ERR_PARAM;
    if (osal_irq_is_in_isr()) return OSAL_ERR_ISR;
    uint32_t start = osal_timer_get_uptime_ms();
    while (1) {
        osal_status_t res = osal_queue_recv(q, item);
        if (res == OSAL_OK) return OSAL_OK;
        if ((uint32_t)(osal_timer_get_uptime_ms() - start) >= timeout_ms) return OSAL_ERR_TIMEOUT;
        osal_task_yield();
    }
}

/* ISR-oriented send path that assumes the caller already owns interrupt context. */
osal_status_t osal_queue_send_from_isr(osal_queue_t *q, const void *item) {
    if (!q || !item) return OSAL_ERR_PARAM;
    // ISR-safe: directly enqueue, assume caller in IRQ context
    return _enqueue(q, item);
}

/* ISR-oriented receive path that assumes the caller already owns interrupt context. */
osal_status_t osal_queue_recv_from_isr(osal_queue_t *q, void *item) {
    if (!q || !item) return OSAL_ERR_PARAM;
    return _dequeue(q, item);
}
