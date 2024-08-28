#include "shared-gpio.h"
#include "gpio.h"

#define GPIO_AF_UART                    1   // the same for MOD1 and MOD2

static const gpio_pin_t mod_gpios[MOD_COUNT][MOD_GPIO_COUNT] = {
    [MOD1] = {
        [MOD_GPIO_TX] = { GPIOA, 9 },
        [MOD_GPIO_RX] = { GPIOA, 10 },
        [MOD_GPIO_DE] = { GPIOA, 12 },
    },
    [MOD2] = {
        [MOD_GPIO_TX] = { GPIOA, 2 },
        [MOD_GPIO_RX] = { GPIOA, 15 },
        [MOD_GPIO_DE] = { GPIOA, 1 },
    },
};

static enum mod_gpio_mode mod_gpio_modes[MOD_COUNT][MOD_GPIO_COUNT];

void shared_gpio_set_mode(enum mod mod, enum mod_gpio mod_gpio, enum mod_gpio_mode mode)
{
    if (mode == mod_gpio_modes[mod][mod_gpio]) {
        return;
    }

    mod_gpio_modes[mod][mod_gpio] = mode;

    const gpio_pin_t g = mod_gpios[mod][mod_gpio];

    switch (mode) {
    default:
    case MOD_GPIO_MODE_INPUT:
        GPIO_S_SET_INPUT(g);
        break;

    case MOD_GPIO_MODE_OUTPUT:
        GPIO_S_SET_OUTPUT(g);
        GPIO_S_SET_PUSHPULL(g);
        break;

    case MOD_GPIO_MODE_OPENDRAIN:
        GPIO_S_SET_OD(g);
        break;

    case MOD_GPIO_MODE_AF_UART:
        GPIO_S_SET_AF(g, GPIO_AF_UART);
        break;
    }
}

enum mod_gpio_mode shared_gpio_get_mode(enum mod mod, enum mod_gpio mod_gpio)
{
    return mod_gpio_modes[mod][mod_gpio];
}

bool shared_gpio_test(enum mod mod, enum mod_gpio mod_gpio)
{
    if (mod_gpio_modes[mod][mod_gpio] >= MOD_GPIO_MODE_AF_UART) {
        return false;
    }

    gpio_pin_t g = mod_gpios[mod][mod_gpio];

    if (GPIO_S_TEST(g)) {
        return true;
    }

    return false;
}

void shared_gpio_set_value(enum mod mod, enum mod_gpio mod_gpio, bool value)
{
    if ((mod_gpio_modes[mod][mod_gpio] != MOD_GPIO_MODE_OUTPUT) ||
        (mod_gpio_modes[mod][mod_gpio] == MOD_GPIO_MODE_OPENDRAIN))
    {
        return;
    }
    gpio_pin_t g = mod_gpios[mod][mod_gpio];

    if (value) {
        GPIO_S_SET(g);
    } else {
        GPIO_S_RESET(g);
    }
}

void shared_gpio_init(void)
{
    for (size_t i = 0; i < MOD_COUNT; i++) {
        for (size_t j = 0; j < MOD_GPIO_COUNT; j++) {
            shared_gpio_set_mode(i, j, MOD_GPIO_MODE_DEFAULT);
        }
    }
}
