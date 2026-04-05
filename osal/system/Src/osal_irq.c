#include "../Inc/osal_irq.h"
#include "../Inc/osal_platform.h"

/* 函数说明：关闭全局中断并返回当前中断状态快照。 */
uint32_t osal_irq_disable(void) {
    return OSAL_PLATFORM_IRQ_DISABLE();
}

/* 函数说明：重新打开全局中断。 */
void osal_irq_enable(void) {
    OSAL_PLATFORM_IRQ_ENABLE();
}

/* 函数说明：按之前保存的状态恢复中断开关。 */
void osal_irq_restore(uint32_t prev_state) {
    OSAL_PLATFORM_IRQ_RESTORE(prev_state);
}

/* 函数说明：判断当前代码是否运行在中断上下文中。 */
bool osal_irq_is_in_isr(void) {
    return OSAL_PLATFORM_IRQ_IS_IN_ISR();
}
