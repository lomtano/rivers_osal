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
 */
uint32_t osal_irq_disable(void);

/**
 * @brief 重新打开全局中断。
 */
void osal_irq_enable(void);

/**
 * @brief 按之前保存的状态恢复中断开关。
 * @param prev_state 由 osal_irq_disable() 返回的状态快照。
 */
void osal_irq_restore(uint32_t prev_state);

/**
 * @brief 判断当前代码是否运行在中断上下文中。
 * @return 运行在 ISR 中返回 true。
 */
bool osal_irq_is_in_isr(void);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_IRQ_H */
