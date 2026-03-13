#include "utest_irq.h"
#include <string.h>

// Internal state for mock IRQ subsystem
static struct {
    irq_flags_t flags;
    irq_flags_t mask;
} irq_state;

void utest_irq_reset(void)
{
    memset(&irq_state, 0, sizeof(irq_state));
}

bool utest_irq_is_flag_set(enum irq_flag f)
{
    if (f >= IRQ_COUNT) {
        return false;
    }
    return (irq_state.flags & (1 << f)) != 0;
}

irq_flags_t utest_irq_get_all_flags(void)
{
    return irq_state.flags;
}

// Mock implementation of IRQ API
irq_flags_t irq_get_flags(void)
{
    return irq_state.flags;
}

void irq_set_flag(enum irq_flag f)
{
    if (f < IRQ_COUNT) {
        irq_state.flags |= (1 << f);
    }
}

void irq_set_mask(irq_flags_t m)
{
    irq_state.mask = m;
}

void irq_clear_flags(irq_flags_t f)
{
    irq_state.flags &= ~f;
}

void irq_init(void)
{
    utest_irq_reset();
}

void irq_do_periodic_work(void)
{
    // Stub implementation
}
