#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "irq-subsystem.h"

// Мок-функции для тестирования подсистемы IRQ
void utest_irq_reset(void);
bool utest_irq_is_flag_set(enum irq_flag f);
irq_flags_t utest_irq_get_all_flags(void);
