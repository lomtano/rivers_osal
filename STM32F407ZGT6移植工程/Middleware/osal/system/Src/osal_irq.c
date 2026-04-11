#include "../Inc/osal_irq.h"
#include "../Inc/osal_platform.h"

/* 函数说明：关闭全局中断并返回当前中断状态快照。 */
uint32_t osal_irq_disable(void) {
    /* 真正的关中断动作由 osal_platform.h 里的宏替换控制。 */
    return OSAL_PLATFORM_IRQ_DISABLE();
}

/* 函数说明：重新打开全局中断。 */
void osal_irq_enable(void) {
    /* 这里是无条件开中断，用于少数明确需要“直接打开中断”的场景。 */
    OSAL_PLATFORM_IRQ_ENABLE();
}

/* 函数说明：按之前保存的状态恢复中断开关。 */
void osal_irq_restore(uint32_t prev_state) {
    /* restore 语义比 enable 更安全，因为它会回到进入临界区前的状态。 */
    OSAL_PLATFORM_IRQ_RESTORE(prev_state);
}

/* 函数说明：判断当前代码是否运行在中断上下文中。 */
bool osal_irq_is_in_isr(void) {
    /* 真正的上下文判断方式同样由 platform 宏决定，例如读 IPSR。 */
    return OSAL_PLATFORM_IRQ_IS_IN_ISR();
}

