#include "../Inc/osal.h"

#if OSAL_CFG_ENABLE_MUTEX

#include "../Inc/osal_mutex.h"
#include "../Inc/osal_irq.h"
#include "../Inc/osal_mem.h"
#include "../Inc/osal_task.h"
#include "../Inc/osal_timer.h"
#include <stdbool.h>

struct osal_mutex {
    volatile bool locked;
    struct osal_mutex *next;
};

static osal_mutex_t *s_mutex_list = NULL;

static void osal_mutex_report(const char *message) {
    OSAL_DEBUG_REPORT("mutex", message);
}

static bool osal_mutex_contains(osal_mutex_t *mutex) {
    osal_mutex_t *current = s_mutex_list;

    while (current != NULL) {
        if (current == mutex) {
            return true;
        }
        current = current->next;
    }

    return false;
}

static bool osal_mutex_validate_handle(const osal_mutex_t *mutex) {
    if (mutex == NULL) {
        return false;
    }
#if OSAL_CFG_ENABLE_DEBUG
    if (!osal_mutex_contains((osal_mutex_t *)mutex)) {
        osal_mutex_report("API called with inactive mutex handle");
        return false;
    }
#endif
    return true;
}

osal_mutex_t *osal_mutex_create(void) {
    osal_mutex_t *mutex;

    if (osal_irq_is_in_isr()) {
        osal_mutex_report("create is not allowed in ISR context");
        return NULL;
    }

    mutex = (osal_mutex_t *)osal_mem_alloc((uint32_t)sizeof(osal_mutex_t));
    if (mutex == NULL) {
        return NULL;
    }
    mutex->locked = false;
    mutex->next = s_mutex_list;
    s_mutex_list = mutex;
    return mutex;
}

void osal_mutex_delete(osal_mutex_t *mutex) {
    osal_mutex_t *prev = NULL;
    osal_mutex_t *current = s_mutex_list;

    if (mutex == NULL) {
        return;
    }
    if (osal_irq_is_in_isr()) {
        osal_mutex_report("delete is not allowed in ISR context");
        return;
    }

    while (current != NULL) {
        if (current == mutex) {
            if (prev == NULL) {
                s_mutex_list = current->next;
            } else {
                prev->next = current->next;
            }
            osal_mem_free(current);
            return;
        }
        prev = current;
        current = current->next;
    }

    osal_mutex_report("delete called with inactive mutex handle");
}

osal_status_t osal_mutex_lock(osal_mutex_t *mutex, uint32_t timeout_ms) {
    uint32_t irq_state;
    uint32_t start;

    if (!osal_mutex_validate_handle(mutex)) {
        return OSAL_ERR_PARAM;
    }
    if (osal_irq_is_in_isr()) {
        osal_mutex_report("lock is not allowed in ISR context");
        return OSAL_ERR_ISR;
    }

    start = osal_timer_get_uptime_ms();
    while (1) {
        irq_state = osal_irq_disable();
        if (!mutex->locked) {
            mutex->locked = true;
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

osal_status_t osal_mutex_unlock(osal_mutex_t *mutex) {
    uint32_t irq_state;

    if (!osal_mutex_validate_handle(mutex)) {
        return OSAL_ERR_PARAM;
    }
    if (osal_irq_is_in_isr()) {
        osal_mutex_report("unlock is not allowed in ISR context");
        return OSAL_ERR_ISR;
    }

    irq_state = osal_irq_disable();
    mutex->locked = false;
    osal_irq_restore(irq_state);
    return OSAL_OK;
}

#endif /* OSAL_CFG_ENABLE_MUTEX */