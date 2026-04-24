#ifndef OSAL_IRQ_H
#define OSAL_IRQ_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 关闭全局中断并返回当前中断状态快照。
 * @return 供 osal_irq_restore() 使用的中断状态。
 * @note 返回值不是固定的开/关标志，而是进入临界区前的原始状态快照。
 */
uint32_t osal_irq_disable(void);

/**
 * @brief 无条件重新打开全局中断。
 * @note 它和 osal_irq_restore(prev_state) 的语义不同。
 */
void osal_irq_enable(void);

/**
 * @brief 按之前保存的状态恢复中断开关。
 * @param prev_state 由 osal_irq_disable() 返回的状态快照。
 * @note 只有进入临界区前原本是开中断状态时，restore 才会重新打开中断。
 */
void osal_irq_restore(uint32_t prev_state);

/**
 * @brief 判断当前代码是否运行在中断上下文中。
 * @return 运行在 ISR 中返回 true。
 * @note system 层很多 API 会先用这个接口判断自己是否允许在 ISR 中调用。
 */
bool osal_irq_is_in_isr(void);

/*
 * 这组 helper 只给 system 层内部使用。
 * 它们不是公开的等待/唤醒接口。
 * mem/queue/timer 通过它们标记已知内核临界区，
 * 这样 cortexm DWT profiling 统计到的是 OSAL 内核代码，而不是应用代码。
 */
void osal_cortexm_profile_enter_internal(void);
void osal_cortexm_profile_exit_internal(void);

static inline uint32_t osal_internal_critical_enter(void) {
    uint32_t prev_state = osal_irq_disable();
    osal_cortexm_profile_enter_internal();
    return prev_state;
}

static inline void osal_internal_critical_exit(uint32_t prev_state) {
    osal_cortexm_profile_exit_internal();
    osal_irq_restore(prev_state);
}

#ifdef __cplusplus
}
#endif

#endif /* OSAL_IRQ_H */
