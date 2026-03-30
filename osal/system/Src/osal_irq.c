#include "../Inc/osal_irq.h"

#if defined(__GNUC__) || defined(__clang__)
#define OSAL_WEAK __attribute__((weak))
#elif defined(__ICCARM__) || defined(__CC_ARM)
#define OSAL_WEAK __weak
#else
#define OSAL_WEAK
#endif

OSAL_WEAK uint32_t osal_irq_disable(void) {
    return 0U;
}

OSAL_WEAK void osal_irq_enable(void) {
}

OSAL_WEAK void osal_irq_restore(uint32_t prev_state) {
    (void)prev_state;
}

OSAL_WEAK bool osal_irq_is_in_isr(void) {
    return false;
}
