#ifndef OSAL_IRQ_H
#define OSAL_IRQ_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 进入临界区并关闭中断�? * @return 用于后续 osal_irq_restore() 恢复的中断状态�? */
uint32_t osal_irq_disable(void);

/**
 * @brief 无条件打开中断�? */
void osal_irq_enable(void);

/**
 * @brief 恢复之前保存的中断状态�? * @param prev_state �?osal_irq_disable() 返回的状态值�? */
void osal_irq_restore(uint32_t prev_state);

/**
 * @brief 判断当前是否运行在中断上下文中�? * @return 处于中断上下文时返回 true�? */
bool osal_irq_is_in_isr(void);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_IRQ_H */
