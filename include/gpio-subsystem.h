#pragma once

#include <stdbool.h>

void gpio_init(void);
void gpio_reset(void);
void gpio_do_periodic_work(void);

// suspend-to-off: при on = true выключает V_OUT и держит его выключенным на всё
// окно сна (защита по V_IN в Stop не работает); при on = false возвращает
// штатное управление V_OUT.
void gpio_suspend(bool on);
