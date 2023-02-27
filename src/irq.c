#include "irq.h"
#include <assert.h>

static_assert((sizeof(irq_flags_t) * 8) >= IRQ_COUNT, "IRQ flags not fitted to `irq_flags` type");

static irq_flags_t flags = 0;
static irq_flags_t mask = 0;

irq_flags_t irq_get_flags(void)
{
    return flags;
}

void irq_set_flag(enum irq_flag f)
{
    flags |= (1 << f);
}

void irq_set_mask(irq_flags_t m)
{
    mask = m;
}

void irq_clear_flag(enum irq_flag f)
{
    flags &= ~(1 << f);
}

void irq_clear_flags(irq_flags_t f)
{
    flags &= ~f;
}

bool irq_is_masked_irq(void)
{
    return flags & mask;
}
