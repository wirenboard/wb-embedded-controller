#pragma once

#include "gpio.h"

typedef enum {
    GPIO_MODE_INPUT = 0,
    GPIO_MODE_OUTPUT = 1,
    GPIO_MODE_AF = 2,
    GPIO_MODE_ANALOG = 3
} gpio_mode_t;

typedef enum {
    GPIO_OTYPE_PUSH_PULL = 0,
    GPIO_OTYPE_OPEN_DRAIN = 1
} gpio_output_type_t;

void utest_gpio_reset_instances(void);

// Функции запроса состояний GPIO для тестирования
uint32_t utest_gpio_get_mode(const gpio_pin_t pin);
uint32_t utest_gpio_get_output_type(const gpio_pin_t pin);
uint32_t utest_gpio_get_output_state(const gpio_pin_t pin);
void utest_gpio_set_input_state(const gpio_pin_t pin, uint32_t state);
