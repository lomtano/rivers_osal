#include "../Inc/osal_irq.h"

#if defined(__GNUC__) || defined(__clang__)
#define OSAL_WEAK __attribute__((weak))
#elif defined(__ICCARM__) || defined(__CC_ARM)
#define OSAL_WEAK __weak
#else
#define OSAL_WEAK
#endif

/* 函数说明：关闭中断并返回当前中断状态快照。 */
OSAL_WEAK uint32_t osal_irq_disable(void) {
    return 0U;
}

/* 函数说明：重新打开全局中断。 */
OSAL_WEAK void osal_irq_enable(void) {
}

/* 函数说明：按之前保存的状态恢复中断开关。 */
OSAL_WEAK void osal_irq_restore(uint32_t prev_state) {
    (void)prev_state;
}

/* 函数说明：判断当前代码是否运行在中断上下文中。 */
OSAL_WEAK bool osal_irq_is_in_isr(void) {
    return false;
}
