#include "utest_shared_gpio.h"
#include <string.h>

#ifdef EC_MOD1_MOD2_GPIO_CONTROL

// Внутреннее состояние мока
static struct {
    enum mod_gpio_mode mode[MOD_COUNT][MOD_GPIO_COUNT];
    bool input_value[MOD_COUNT][MOD_GPIO_COUNT];
    bool output_value[MOD_COUNT][MOD_GPIO_COUNT];
} shared_gpio_state;

void utest_shared_gpio_reset(void)
{
    memset(&shared_gpio_state, 0, sizeof(shared_gpio_state));
    // Установка режима INPUT по умолчанию
    for (unsigned mod = 0; mod < MOD_COUNT; mod++) {
        for (unsigned gpio = 0; gpio < MOD_GPIO_COUNT; gpio++) {
            shared_gpio_state.mode[mod][gpio] = MOD_GPIO_MODE_INPUT;
        }
    }
}

void utest_shared_gpio_set_input_value(enum mod mod, enum mod_gpio mod_gpio, bool value)
{
    if (mod < MOD_COUNT && mod_gpio < MOD_GPIO_COUNT) {
        shared_gpio_state.input_value[mod][mod_gpio] = value;
    }
}

bool utest_shared_gpio_get_output_value(enum mod mod, enum mod_gpio mod_gpio)
{
    if (mod < MOD_COUNT && mod_gpio < MOD_GPIO_COUNT) {
        return shared_gpio_state.output_value[mod][mod_gpio];
    }
    return false;
}

enum mod_gpio_mode utest_shared_gpio_get_mode(enum mod mod, enum mod_gpio mod_gpio)
{
    if (mod < MOD_COUNT && mod_gpio < MOD_GPIO_COUNT) {
        return shared_gpio_state.mode[mod][mod_gpio];
    }
    return MOD_GPIO_MODE_INPUT;
}

// Мок-реализация shared-gpio API
void shared_gpio_init(void)
{
    utest_shared_gpio_reset();
}

void shared_gpio_set_mode(enum mod mod, enum mod_gpio mod_gpio, enum mod_gpio_mode mode)
{
    if (mod < MOD_COUNT && mod_gpio < MOD_GPIO_COUNT) {
        shared_gpio_state.mode[mod][mod_gpio] = mode;
    }
}

enum mod_gpio_mode shared_gpio_get_mode(enum mod mod, enum mod_gpio mod_gpio)
{
    if (mod < MOD_COUNT && mod_gpio < MOD_GPIO_COUNT) {
        return shared_gpio_state.mode[mod][mod_gpio];
    }
    return MOD_GPIO_MODE_DEFAULT;
}

bool shared_gpio_test(enum mod mod, enum mod_gpio mod_gpio)
{
    if (mod < MOD_COUNT && mod_gpio < MOD_GPIO_COUNT) {
        return shared_gpio_state.input_value[mod][mod_gpio];
    }
    return false;
}

void shared_gpio_set_value(enum mod mod, enum mod_gpio mod_gpio, bool value)
{
    if (mod < MOD_COUNT && mod_gpio < MOD_GPIO_COUNT) {
        shared_gpio_state.output_value[mod][mod_gpio] = value;
    }
}

#endif
