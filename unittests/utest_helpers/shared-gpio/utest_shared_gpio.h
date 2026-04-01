#pragma once
#include <stdbool.h>
#include "shared-gpio.h"

#ifdef EC_MOD1_MOD2_GPIO_CONTROL

// Функции для тестирования
void utest_shared_gpio_reset(void);
void utest_shared_gpio_set_input_value(enum mod mod, enum mod_gpio mod_gpio, bool value);
bool utest_shared_gpio_get_output_value(enum mod mod, enum mod_gpio mod_gpio);
enum mod_gpio_mode utest_shared_gpio_get_mode(enum mod mod, enum mod_gpio mod_gpio);

#endif
