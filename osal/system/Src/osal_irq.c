/*
 * osal_irq.c
 * CPU interrupt control abstraction.
 * - Default no-op implementation.
 * - Platform layer should provide real lock/unlock.
 */

#include "../Inc/osal_irq.h"

#if defined(__GNUC__) || defined(__clang__)
#define OSAL_WEAK __attribute__((weak))
#elif defined(__ICCARM__) || defined(__CC_ARM)
#define OSAL_WEAK __weak
#else
#define OSAL_WEAK
#endif

/* Weak default critical-section entry hook for non-integrated builds. */
OSAL_WEAK uint32_t osal_irq_disable(void) {
    return 0U;
}

/* Weak default global interrupt enable hook for non-integrated builds. */
OSAL_WEAK void osal_irq_enable(void) {
}

/* Weak default critical-section exit hook for non-integrated builds. */
OSAL_WEAK void osal_irq_restore(uint32_t prev_state) {
    (void)prev_state;
}

/* Weak default ISR-context detector for non-integrated builds. */
OSAL_WEAK bool osal_irq_is_in_isr(void) {
    return false;
}
