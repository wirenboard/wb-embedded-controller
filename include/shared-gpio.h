#pragma once
#include <stdint.h>
#include <stdbool.h>

enum mod {
    MOD1,
    MOD2,

    MOD_COUNT
};

enum mod_gpio {
    MOD_GPIO_TX,
    MOD_GPIO_RX,
    MOD_GPIO_DE,

    MOD_GPIO_COUNT
};

enum mod_gpio_mode {
    MOD_GPIO_MODE_INPUT,
    MOD_GPIO_MODE_OUTPUT,
    MOD_GPIO_MODE_OPENDRAIN,
    MOD_GPIO_MODE_AF_UART,
    MOD_GPIO_MODE_PA9_AF_DEBUG_UART,    // ony for MOD1_TX (PA9) - GPIO is shared between DEBUG_UART and MOD1

    MOD_GPIO_MODE_COUNT,
    MOD_GPIO_MODE_DEFAULT = MOD_GPIO_MODE_INPUT
};

void shared_gpio_init(void);

void shared_gpio_set_mode(enum mod mod, enum mod_gpio mod_gpio, enum mod_gpio_mode mode);
enum mod_gpio_mode shared_gpio_get_mode(enum mod mod, enum mod_gpio mod_gpio);

bool shared_gpio_test(enum mod mod, enum mod_gpio mod_gpio);
void shared_gpio_set_value(enum mod mod, enum mod_gpio mod_gpio, bool value);
