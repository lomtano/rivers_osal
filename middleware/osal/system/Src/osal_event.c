/*
 * osal_event.c
 * Simple event synchronization object.
 * - Supports auto-reset/event-set semantics
 * - Busy-wait polling with timeout
 */

#include "../Inc/osal_event.h"
#include "../Inc/osal_irq.h"
#include "../Inc/osal_mem.h"
#include "../Inc/osal_task.h"
#include "../Inc/osal_timer.h"

struct osal_event {
    bool state;
    bool auto_reset;
    struct osal_event *next;
};

static osal_event_t *s_event_list = NULL;

/* Allocate an event control block from the unified OSAL heap. */
osal_event_t *osal_event_create(bool auto_reset) {
    osal_event_t *evt = (osal_event_t *)osal_mem_alloc((uint32_t)sizeof(osal_event_t));
    if (evt == NULL) return NULL;
    evt->state = false;
    evt->auto_reset = auto_reset;
    evt->next = s_event_list;
    s_event_list = evt;
    return evt;
}

/* Remove an event from the internal list and free it. */
void osal_event_delete(osal_event_t *evt) {
    osal_event_t *prev = NULL;
    osal_event_t *current = s_event_list;

    if (!evt) return;
    while (current != NULL) {
        if (current == evt) {
            if (prev == NULL) {
                s_event_list = current->next;
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

/* Set an event to the signaled state. */
osal_status_t osal_event_set(osal_event_t *evt) {
    uint32_t irq_state;

    if (!evt) return OSAL_ERR_PARAM;
    irq_state = osal_irq_disable();
    evt->state = true;
    osal_irq_restore(irq_state);
    return OSAL_OK;
}

/* Clear an event back to the non-signaled state. */
osal_status_t osal_event_clear(osal_event_t *evt) {
    uint32_t irq_state;

    if (!evt) return OSAL_ERR_PARAM;
    irq_state = osal_irq_disable();
    evt->state = false;
    osal_irq_restore(irq_state);
    return OSAL_OK;
}

/* Wait cooperatively until an event is signaled or a timeout expires. */
osal_status_t osal_event_wait(osal_event_t *evt, uint32_t timeout_ms) {
    uint32_t irq_state;

    if (!evt) return OSAL_ERR_PARAM;
    if (osal_irq_is_in_isr()) return OSAL_ERR_ISR;
    uint32_t start = osal_timer_get_uptime_ms();
    while (1) {
        irq_state = osal_irq_disable();
        if (evt->state) {
            if (evt->auto_reset) evt->state = false;
            osal_irq_restore(irq_state);
            return OSAL_OK;
        }
        osal_irq_restore(irq_state);
        if ((uint32_t)(osal_timer_get_uptime_ms() - start) >= timeout_ms) {
            return OSAL_ERR_TIMEOUT;
        }
        osal_task_yield();
    }
}
