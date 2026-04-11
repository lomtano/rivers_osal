#include "../Inc/osal.h"

#if OSAL_CFG_ENABLE_MUTEX

#include "../Inc/osal_mutex.h"
#include "../Inc/osal_irq.h"
#include "../Inc/osal_mem.h"
#include "../Inc/osal_task.h"
#include "../Inc/osal_timer.h"
#include <stdbool.h>

/*
 * 这是最小互斥量实现：
 * 1. locked 只表示“是否已被占用”，不记录 owner。
 * 2. next 仅用于活动对象链表管理。
 */
struct osal_mutex {
    volatile bool locked;
    struct osal_mutex *next;
};

static osal_mutex_t *s_mutex_list = NULL;

/* 函数说明：输出互斥量模块调试诊断信息。 */
static void osal_mutex_report(const char *message) {
    OSAL_DEBUG_REPORT("mutex", message);
}

/* 函数说明：检查互斥量句柄是否仍在活动链表中。 */
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

/* 函数说明：校验互斥量句柄是否有效。 */
static bool osal_mutex_validate_handle(const osal_mutex_t *mutex) {
    if (mutex == NULL) {
        return false;
    }
#if OSAL_CFG_ENABLE_DEBUG
    if (!osal_mutex_contains((osal_mutex_t *)mutex)) {
        /* debug 模式下用活动链表防止“已经 delete 的句柄继续拿来 lock/unlock”。 */
        osal_mutex_report("API called with inactive mutex handle");
        return false;
    }
#endif
    return true;
}

/* 函数说明：创建一个互斥量对象。 */
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
    /* 新建互斥量默认是未上锁状态。 */
    mutex->locked = false;
    mutex->next = s_mutex_list;
    s_mutex_list = mutex;
    return mutex;
}

/* 函数说明：删除一个互斥量对象。 */
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
                /* 删除的是活动链表头。 */
                s_mutex_list = current->next;
            } else {
                /* 删除的是中间节点。 */
                prev->next = current->next;
            }
            /* 当前最小实现不检查“删除时是否仍处于锁定状态”，直接释放控制块。 */
            osal_mem_free(current);
            return;
        }
        prev = current;
        current = current->next;
    }

    osal_mutex_report("delete called with inactive mutex handle");
}

/*
 * 当前 mutex_lock 不是事件驱动等待：
 * - 失败时不会挂等待链表
 * - 而是 yield 后重试
 * 因此它适合低竞争场景，不适合复杂优先级反转治理。
 */
/* 函数说明：尝试获取互斥量并按需等待超时。 */
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
            /* 看到 unlocked 后，立刻在临界区内改成 true，避免并发竞争。 */
            mutex->locked = true;
            osal_irq_restore(irq_state);
            return OSAL_OK;
        }
        osal_irq_restore(irq_state);

        if ((uint32_t)(osal_timer_get_uptime_ms() - start) >= timeout_ms) {
            /* 同样采用差值比较，兼容 32 位 tick 回绕。 */
            return OSAL_ERR_TIMEOUT;
        }
        /*
         * 锁还没释放时，不是忙等空转，而是把执行权让给别的任务。
         * 但它依旧不是队列那种“等待链表 + 事件唤醒”模型。
         */
        osal_task_yield();
    }
}

/* 函数说明：释放已经获取到的互斥量。 */
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
    /* 当前实现没有 owner 检查，因此任何拿到句柄的任务都能把它解锁。 */
    mutex->locked = false;
    osal_irq_restore(irq_state);
    return OSAL_OK;
}

#endif /* OSAL_CFG_ENABLE_MUTEX */




