#include "../Inc/osal.h"

#if OSAL_CFG_ENABLE_QUEUE

#include "../Inc/osal_queue.h"
#include "../Inc/osal_mem.h"
#include "../Inc/osal_task.h"
#include <stdbool.h>
#include <string.h>

struct osal_queue {
    uint8_t *storage;
    uint32_t head;
    uint32_t tail;
    uint32_t length;
    uint32_t item_size;
    uint32_t count;
    bool owns_storage;
    struct osal_queue *next;
};

static osal_queue_t *s_queue_list = NULL;

static void osal_queue_report(const char *message) {
    OSAL_DEBUG_REPORT("queue", message);
}

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

static void osal_queue_link(osal_queue_t *q) {
    q->next = s_queue_list;
    s_queue_list = q;
}

static osal_queue_t *osal_queue_create_internal(uint8_t *storage, uint32_t length, uint32_t item_size, bool owns_storage) {
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
    osal_queue_link(q);
    return q;
}

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

void osal_queue_delete(osal_queue_t *q) {
    osal_queue_t *prev = NULL;
    osal_queue_t *current = s_queue_list;

    if (q == NULL) {
        return;
    }
    if (osal_irq_is_in_isr()) {
        osal_queue_report("delete is not allowed in ISR context");
        return;
    }

    while (current != NULL) {
        if (current == q) {
            if (prev == NULL) {
                s_queue_list = current->next;
            } else {
                prev->next = current->next;
            }

            if (current->owns_storage) {
                osal_mem_free(current->storage);
            }
            osal_mem_free(current);
            return;
        }
        prev = current;
        current = current->next;
    }

    osal_queue_report("delete called with inactive queue handle");
}

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

static osal_status_t osal_queue_enqueue(osal_queue_t *q, const void *item) {
    if (q->count >= q->length) {
        return OSAL_ERR_RESOURCE;
    }

    memcpy(q->storage + (q->tail * q->item_size), item, q->item_size);
    q->tail = (q->tail + 1U) % q->length;
    q->count++;
    return OSAL_OK;
}

static osal_status_t osal_queue_dequeue(osal_queue_t *q, void *item) {
    if (q->count == 0U) {
        return OSAL_ERR_RESOURCE;
    }

    memcpy(item, q->storage + (q->head * q->item_size), q->item_size);
    q->head = (q->head + 1U) % q->length;
    q->count--;
    return OSAL_OK;
}

osal_status_t osal_queue_send(osal_queue_t *q, const void *item) {
    uint32_t irq_state;
    osal_status_t res;

    if ((!osal_queue_validate_handle(q)) || (item == NULL)) {
        return OSAL_ERR_PARAM;
    }

    irq_state = osal_irq_disable();
    res = osal_queue_enqueue(q, item);
    osal_irq_restore(irq_state);
    return res;
}

osal_status_t osal_queue_recv(osal_queue_t *q, void *item) {
    uint32_t irq_state;
    osal_status_t res;

    if ((!osal_queue_validate_handle(q)) || (item == NULL)) {
        return OSAL_ERR_PARAM;
    }

    irq_state = osal_irq_disable();
    res = osal_queue_dequeue(q, item);
    osal_irq_restore(irq_state);
    return res;
}

osal_status_t osal_queue_send_timeout(osal_queue_t *q, const void *item, uint32_t timeout_ms) {
    uint32_t start;

    if ((!osal_queue_validate_handle(q)) || (item == NULL)) {
        return OSAL_ERR_PARAM;
    }
    if (osal_irq_is_in_isr()) {
        osal_queue_report("send_timeout is not allowed in ISR context");
        return OSAL_ERR_ISR;
    }

    start = osal_timer_get_tick();
    while (1) {
        osal_status_t res = osal_queue_send(q, item);

        if (res == OSAL_OK) {
            return OSAL_OK;
        }
        if ((uint32_t)(osal_timer_get_tick() - start) >= timeout_ms) {
            return OSAL_ERR_TIMEOUT;
        }
        osal_task_yield();
    }
}

osal_status_t osal_queue_recv_timeout(osal_queue_t *q, void *item, uint32_t timeout_ms) {
    uint32_t start;

    if ((!osal_queue_validate_handle(q)) || (item == NULL)) {
        return OSAL_ERR_PARAM;
    }
    if (osal_irq_is_in_isr()) {
        osal_queue_report("recv_timeout is not allowed in ISR context");
        return OSAL_ERR_ISR;
    }

    start = osal_timer_get_tick();
    while (1) {
        osal_status_t res = osal_queue_recv(q, item);

        if (res == OSAL_OK) {
            return OSAL_OK;
        }
        if ((uint32_t)(osal_timer_get_tick() - start) >= timeout_ms) {
            return OSAL_ERR_TIMEOUT;
        }
        osal_task_yield();
    }
}

osal_status_t osal_queue_send_from_isr(osal_queue_t *q, const void *item) {
    if ((!osal_queue_validate_handle(q)) || (item == NULL)) {
        return OSAL_ERR_PARAM;
    }

    return osal_queue_enqueue(q, item);
}

osal_status_t osal_queue_recv_from_isr(osal_queue_t *q, void *item) {
    if ((!osal_queue_validate_handle(q)) || (item == NULL)) {
        return OSAL_ERR_PARAM;
    }

    return osal_queue_dequeue(q, item);
}

#endif /* OSAL_CFG_ENABLE_QUEUE */