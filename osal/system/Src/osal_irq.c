#include "../Inc/osal_irq.h"
#include "../Inc/osal_cortexm.h"

/* 函数说明：关闭全局中断并返回进入临界区前的原始状态快照。 */
uint32_t osal_irq_disable(void) {
    return OSAL_CORTEXM_IRQ_DISABLE();
}

/* 函数说明：无条件重新打开全局中断。 */
void osal_irq_enable(void) {
    OSAL_CORTEXM_IRQ_ENABLE();
}

/* 函数说明：按之前保存的状态快照恢复中断开关。 */
void osal_irq_restore(uint32_t prev_state) {
    OSAL_CORTEXM_IRQ_RESTORE(prev_state);
}

/* 函数说明：判断当前代码是否运行在 ISR 上下文中。 */
bool osal_irq_is_in_isr(void) {
    return OSAL_CORTEXM_IRQ_IS_IN_ISR();
}
