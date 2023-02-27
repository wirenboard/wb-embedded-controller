#pragma once

#include <stdint.h>
#include <stdbool.h>

enum irq_flag {
    IRQ_ALARM,
    IRQ_PWR_OFF_REQ,

    IRQ_COUNT
};

typedef uint8_t irq_flags_t;

irq_flags_t irq_get_flags(void);
void irq_set_flag(enum irq_flag f);
void irq_set_mask(irq_flags_t m);
void irq_clear_flag(enum irq_flag f);
void irq_clear_flags(irq_flags_t f);
bool irq_is_masked_irq(void);
