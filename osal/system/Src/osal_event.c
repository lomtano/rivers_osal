#include "../Inc/osal.h"

#if OSAL_CFG_ENABLE_EVENT

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

static void osal_event_report(const char *message) {
    OSAL_DEBUG_REPORT("event", message);
}

static bool osal_event_contains(osal_event_t *evt) {
    osal_event_t *current = s_event_list;

    while (current != NULL) {
        if (current == evt) {
            return true;
        }
        current = current->next;
    }

    return false;
}

static bool osal_event_validate_handle(const osal_event_t *evt) {
    if (evt == NULL) {
        return false;
    }
#if OSAL_CFG_ENABLE_DEBUG
    if (!osal_event_contains((osal_event_t *)evt)) {
        osal_event_report("API called with inactive event handle");
        return false;
    }
#endif
    return true;
}

osal_event_t *osal_event_create(bool auto_reset) {
    osal_event_t *evt;

    if (osal_irq_is_in_isr()) {
        osal_event_report("create is not allowed in ISR context");
        return NULL;
    }

    evt = (osal_event_t *)osal_mem_alloc((uint32_t)sizeof(osal_event_t));
    if (evt == NULL) {
        return NULL;
    }
    evt->state = false;
    evt->auto_reset = auto_reset;
    evt->next = s_event_list;
    s_event_list = evt;
    return evt;
}

void osal_event_delete(osal_event_t *evt) {
    osal_event_t *prev = NULL;
    osal_event_t *current = s_event_list;

    if (evt == NULL) {
        return;
    }
    if (osal_irq_is_in_isr()) {
        osal_event_report("delete is not allowed in ISR context");
        return;
    }

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

    osal_event_report("delete called with inactive event handle");
}

osal_status_t osal_event_set(osal_event_t *evt) {
    uint32_t irq_state;

    if (!osal_event_validate_handle(evt)) {
        return OSAL_ERR_PARAM;
    }
    irq_state = osal_irq_disable();
    evt->state = true;
    osal_irq_restore(irq_state);
    return OSAL_OK;
}

osal_status_t osal_event_clear(osal_event_t *evt) {
    uint32_t irq_state;

    if (!osal_event_validate_handle(evt)) {
        return OSAL_ERR_PARAM;
    }
    irq_state = osal_irq_disable();
    evt->state = false;
    osal_irq_restore(irq_state);
    return OSAL_OK;
}

osal_status_t osal_event_wait(osal_event_t *evt, uint32_t timeout_ms) {
    uint32_t irq_state;
    uint32_t start;

    if (!osal_event_validate_handle(evt)) {
        return OSAL_ERR_PARAM;
    }
    if (osal_irq_is_in_isr()) {
        osal_event_report("wait is not allowed in ISR context");
        return OSAL_ERR_ISR;
    }

    start = osal_timer_get_uptime_ms();
    while (1) {
        irq_state = osal_irq_disable();
        if (evt->state) {
            if (evt->auto_reset) {
                evt->state = false;
            }
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

#endif /* OSAL_CFG_ENABLE_EVENT */