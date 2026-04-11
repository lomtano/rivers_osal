#include "../Inc/osal.h"

#if OSAL_CFG_ENABLE_EVENT

#include "../Inc/osal_event.h"
#include "../Inc/osal_irq.h"
#include "../Inc/osal_mem.h"
#include "../Inc/osal_task.h"
#include "../Inc/osal_timer.h"

/*
 * 事件对象非常轻量：
 * 1. state 表示当前事件是否处于“已触发”状态。
 * 2. auto_reset 表示被 wait 成功消费后，是否自动清零。
 * 3. next 仅用于活动对象链表管理。
 */
struct osal_event {
    bool state;
    bool auto_reset;
    struct osal_event *next;
};

static osal_event_t *s_event_list = NULL;

/* 函数说明：输出事件模块调试诊断信息。 */
static void osal_event_report(const char *message) {
    OSAL_DEBUG_REPORT("event", message);
}

/* 函数说明：检查事件句柄是否仍在活动链表中。 */
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

/* 函数说明：校验事件句柄是否有效。 */
static bool osal_event_validate_handle(const osal_event_t *evt) {
    if (evt == NULL) {
        return false;
    }
#if OSAL_CFG_ENABLE_DEBUG
    if (!osal_event_contains((osal_event_t *)evt)) {
        /* debug 模式下用活动链表挡住“已删除句柄继续使用”的问题。 */
        osal_event_report("API called with inactive event handle");
        return false;
    }
#endif
    return true;
}

/* 函数说明：创建一个事件对象。 */
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
    /* 新建事件默认未触发。 */
    evt->state = false;
    /* auto_reset=true 时，第一次 wait 成功就会自动把 state 清回 false。 */
    evt->auto_reset = auto_reset;
    evt->next = s_event_list;
    s_event_list = evt;
    return evt;
}

/* 函数说明：删除一个事件对象。 */
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
                /* 删除的是活动链表头时，头指针后移。 */
                s_event_list = current->next;
            } else {
                /* 删除的是中间节点时，让前驱节点跳过它。 */
                prev->next = current->next;
            }
            /* 事件对象本身来自 OSAL 堆，删除时只需释放控制块。 */
            osal_mem_free(current);
            return;
        }
        prev = current;
        current = current->next;
    }

    osal_event_report("delete called with inactive event handle");
}

/* 函数说明：将事件置为已触发状态。 */
osal_status_t osal_event_set(osal_event_t *evt) {
    uint32_t irq_state;

    if (!osal_event_validate_handle(evt)) {
        return OSAL_ERR_PARAM;
    }
    irq_state = osal_irq_disable();
    /* set 的语义就是把事件状态置为“已触发”。 */
    evt->state = true;
    osal_irq_restore(irq_state);
    return OSAL_OK;
}

/* 函数说明：清除事件触发状态。 */
osal_status_t osal_event_clear(osal_event_t *evt) {
    uint32_t irq_state;

    if (!osal_event_validate_handle(evt)) {
        return OSAL_ERR_PARAM;
    }
    irq_state = osal_irq_disable();
    /* clear 直接把触发状态清零。 */
    evt->state = false;
    osal_irq_restore(irq_state);
    return OSAL_OK;
}

/*
 * 当前 event_wait 还不是队列那种“挂等待链表 + 事件驱动唤醒”模型，
 * 而是反复检查条件，不满足时主动 yield 给调度器，让别的任务先运行。
 */
/* 函数说明：等待事件被触发或等待超时。 */
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
                /* 自动复位事件在一次 wait 成功后就消费掉这次触发状态。 */
                evt->state = false;
            }
            osal_irq_restore(irq_state);
            return OSAL_OK;
        }
        osal_irq_restore(irq_state);

        if ((uint32_t)(osal_timer_get_uptime_ms() - start) >= timeout_ms) {
            /* 这里用差值比较，避免 32 位毫秒计数回绕时直接比较失真。 */
            return OSAL_ERR_TIMEOUT;
        }
        /*
         * 当前还没等到事件，就主动让出一次调度机会。
         * 所以 event_wait 不会把整个系统卡死，但它也不是队列那种真正事件驱动等待。
         */
        osal_task_yield();
    }
}

#endif /* OSAL_CFG_ENABLE_EVENT */




