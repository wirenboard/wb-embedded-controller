#pragma once
#include "wbmcu_system.h"

typedef struct {
    GPIO_TypeDef* port;
    uint8_t pin;
} gpio_pin_t;

#define GPIO_S(x)                               {x##_PORT, x##_PIN}

#define GPIO_MODE_INPUT                         0
#define GPIO_MODE_OUTPUT                        1
#define GPIO_MODE_AF                            2
#define GPIO_MODE_ANALOG                        3

#define GPIO_TYPE_PP                            0
#define GPIO_TYPE_OD                            1

#define GPIO_SPEED_LOW                          0
#define GPIO_SPEED_MED                          1
#define GPIO_SPEED_HIGH                         3

#define GPIO_PUPD_NONE                          0
#define GPIO_PUPD_PU                            1
#define GPIO_PUPD_PD                            2

#define BIT_FIELD_MASK(len)                     ((1U << len) - 1U)
#define BIT_FIELD_CLEAN(reg, pos, len)          reg &= ~(BIT_FIELD_MASK(len) << pos)
#define BIT_FIELD_SET(reg, pos, val)            reg |= (val << pos)
#define BIT_FIELD_WRITE(reg, pos, len, val)     BIT_FIELD_CLEAN(reg, pos, len); BIT_FIELD_SET(reg, pos, val)

#define GPIO_SET_INPUT(port, pin)               BIT_FIELD_WRITE(port->MODER, 2 * pin, 2, GPIO_MODE_INPUT)
#define GPIO_SET_OUTPUT(port, pin)              BIT_FIELD_WRITE(port->MODER, 2 * pin, 2, GPIO_MODE_OUTPUT)
#define GPIO_SET_AF(port, pin, af)              BIT_FIELD_WRITE(port->MODER, 2 * pin, 2, GPIO_MODE_AF); \
                                                BIT_FIELD_WRITE(port->AFR[(pin / 8)], ((pin % 8) * 4), 4, af)
#define GPIO_SET_ANALOG(port, pin)              BIT_FIELD_WRITE(port->MODER, 2 * pin, 2, GPIO_MODE_ANALOG)

#define GPIO_SET_NOPULL(port,pin)               BIT_FIELD_WRITE(port->PUPDR, 2 * pin, 2, GPIO_PUPD_NONE)
#define GPIO_SET_PULLDOWN(port,pin)             BIT_FIELD_WRITE(port->PUPDR, 2 * pin, 2, GPIO_PUPD_PD)
#define GPIO_SET_PULLUP(port,pin)               BIT_FIELD_WRITE(port->PUPDR, 2 * pin, 2, GPIO_PUPD_PU)

#define GPIO_SET_SPEED_LOW(port,pin)            BIT_FIELD_WRITE(port->OSPEEDR, 2 * pin, 2, GPIO_SPEED_LOW)
#define GPIO_SET_SPEED_MED(port,pin)            BIT_FIELD_WRITE(port->OSPEEDR, 2 * pin, 2, GPIO_SPEED_MED)
#define GPIO_SET_SPEED_HIGH(port,pin)           BIT_FIELD_WRITE(port->OSPEEDR, 2 * pin, 2, GPIO_SPEED_HIGH)

#define GPIO_SET_PUSHPULL(port,pin)             BIT_FIELD_WRITE(port->OTYPER, 1U * pin, 1U, GPIO_TYPE_PP)
#define GPIO_SET_OD(port,pin)                   BIT_FIELD_WRITE(port->OTYPER, 1 * pin, 1, GPIO_TYPE_OD)

#define GPIO_TEST(port, pin)                    (port->IDR & (1 << pin))
#define GPIO_SET(port, pin)                     port->BSRR |= (1 << pin)
#define GPIO_RESET(port, pin)                   port->BRR |= (1 << pin)
#define GPIO_TOGGLE(port, pin)                  port->ODR ^= (1 << pin)

#define GPIO_S_SET_OUTPUT(gpio)                 GPIO_SET_OUTPUT(gpio.port, gpio.pin)
#define GPIO_S_SET_INPUT(gpio)                  GPIO_SET_INPUT(gpio.port, gpio.pin)
#define GPIO_S_SET_AF(gpio, af)                 GPIO_SET_AF(gpio.port, gpio.pin, af)
#define GPIO_S_SET_SPEED_HIGH(gpio)             GPIO_SET_SPEED_HIGH(gpio.port, gpio.pin)
#define GPIO_S_SET_PULLDOWN(gpio)               GPIO_SET_PULLDOWN(gpio.port, gpio.pin)
#define GPIO_S_SET_PULLUP(gpio)                 GPIO_SET_PULLUP(gpio.port, gpio.pin)
#define GPIO_S_SET_PUSHPULL(gpio)               GPIO_SET_PUSHPULL(gpio.port, gpio.pin)
#define GPIO_S_SET_OD(gpio)                     GPIO_SET_OD(gpio.port, gpio.pin)

#define GPIO_S_TEST(gpio)                       GPIO_TEST(gpio.port, gpio.pin)
#define GPIO_S_SET(gpio)                        GPIO_SET(gpio.port, gpio.pin)
#define GPIO_S_RESET(gpio)                      GPIO_RESET(gpio.port, gpio.pin)
#define GPIO_S_TOGGLE(gpio)                     GPIO_TOGGLE(gpio.port, gpio.pin)
