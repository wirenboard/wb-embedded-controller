#include "gpio.h"
#include <string.h>

// GPIO peripheral instances
GPIO_TypeDef _GPIO_instance[GPIO_INSTANCE_COUNT] = {0};

void utest_gpio_reset_instances(void)
{
    memset(&_GPIO_instance, 0, sizeof(_GPIO_instance));
}

// GPIO state query functions for testing
uint32_t utest_gpio_get_mode(const gpio_pin_t pin)
{
    return (pin.port->MODER >> (pin.pin * 2)) & 0x3;
}

uint32_t utest_gpio_get_output_type(const gpio_pin_t pin)
{
    return (pin.port->OTYPER >> pin.pin) & 0x1;
}

uint32_t utest_gpio_get_output_state(const gpio_pin_t pin)
{
    return (pin.port->ODR >> pin.pin) & 0x1;
}

// GPIO mock function implementations
// These provide basic functional implementations of GPIO operations

void GPIO_S_SET(gpio_pin_t pin)
{
    pin.port->BSRR = (1U << pin.pin);
    pin.port->ODR |= (1U << pin.pin);
}

void GPIO_S_RESET(gpio_pin_t pin)
{
    pin.port->BSRR = (1U << (pin.pin + 16));
    pin.port->ODR &= ~(1U << pin.pin);
}

void GPIO_S_SET_OUTPUT(gpio_pin_t pin)
{
    pin.port->MODER &= ~(3U << (pin.pin * 2));
    pin.port->MODER |= (1U << (pin.pin * 2));
}

void GPIO_S_SET_INPUT(gpio_pin_t pin)
{
    pin.port->MODER &= ~(3U << (pin.pin * 2));
}

void GPIO_S_SET_PUSHPULL(gpio_pin_t pin)
{
    pin.port->OTYPER &= ~(1U << pin.pin);
}

void GPIO_S_SET_OD(gpio_pin_t pin)
{
    pin.port->OTYPER |= (1U << pin.pin);
}

void GPIO_S_SET_PULLUP(gpio_pin_t pin)
{
    pin.port->PUPDR &= ~(3U << (pin.pin * 2));
    pin.port->PUPDR |= (1U << (pin.pin * 2));
}

void GPIO_S_SET_PULLDOWN(gpio_pin_t pin)
{
    pin.port->PUPDR &= ~(3U << (pin.pin * 2));
    pin.port->PUPDR |= (2U << (pin.pin * 2));
}

void GPIO_S_SET_NOPULL(gpio_pin_t pin)
{
    pin.port->PUPDR &= ~(3U << (pin.pin * 2));
}

void GPIO_S_SET_SPEED_LOW(gpio_pin_t pin)
{
    pin.port->OSPEEDR &= ~(3U << (pin.pin * 2));
}

void GPIO_S_SET_SPEED_MED(gpio_pin_t pin)
{
    pin.port->OSPEEDR &= ~(3U << (pin.pin * 2));
    pin.port->OSPEEDR |= (1U << (pin.pin * 2));
}

void GPIO_S_SET_SPEED_HIGH(gpio_pin_t pin)
{
    pin.port->OSPEEDR &= ~(3U << (pin.pin * 2));
    pin.port->OSPEEDR |= (3U << (pin.pin * 2));
}

void GPIO_S_SET_ANALOG(gpio_pin_t pin)
{
    pin.port->MODER |= (3U << (pin.pin * 2));
}

void GPIO_S_SET_AF(gpio_pin_t pin, uint8_t af)
{
    uint8_t afr_idx = pin.pin / 8;
    uint8_t afr_pos = (pin.pin % 8) * 4;

    pin.port->AFR[afr_idx] &= ~(0xFU << afr_pos);
    pin.port->AFR[afr_idx] |= ((uint32_t)af << afr_pos);
}
