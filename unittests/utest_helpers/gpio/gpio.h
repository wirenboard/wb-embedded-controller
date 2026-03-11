#pragma once

#include <stdint.h>

#define GPIO_INSTANCE_COUNT         4

#define __IO volatile

#define BIT_FIELD_MASK(len)                     ((1U << len) - 1U)

typedef struct {
    __IO uint32_t MODER;       /*!< GPIO port mode register,               Address offset: 0x00      */
    __IO uint32_t OTYPER;      /*!< GPIO port output type register,        Address offset: 0x04      */
    __IO uint32_t OSPEEDR;     /*!< GPIO port output speed register,       Address offset: 0x08      */
    __IO uint32_t PUPDR;       /*!< GPIO port pull-up/pull-down register,  Address offset: 0x0C      */
    __IO uint32_t IDR;         /*!< GPIO port input data register,         Address offset: 0x10      */
    __IO uint32_t ODR;         /*!< GPIO port output data register,        Address offset: 0x14      */
    __IO uint32_t BSRR;        /*!< GPIO port bit set/reset  register,     Address offset: 0x18      */
    __IO uint32_t AFR[2];      /*!< GPIO alternate function registers,     Address offset: 0x20-0x24 */
    __IO uint32_t BRR;         /*!< GPIO Bit Reset register,               Address offset: 0x28      */
} GPIO_TypeDef;

extern GPIO_TypeDef _GPIO_instance[GPIO_INSTANCE_COUNT];

#define GPIOA (&_GPIO_instance[0])
#define GPIOB (&_GPIO_instance[1])
#define GPIOC (&_GPIO_instance[2])
#define GPIOD (&_GPIO_instance[3])

typedef struct {
    GPIO_TypeDef* port;
    uint8_t pin;
} gpio_pin_t;

// GPIO mock function declarations
void GPIO_S_SET(gpio_pin_t pin);
void GPIO_S_RESET(gpio_pin_t pin);
void GPIO_S_SET_OUTPUT(gpio_pin_t pin);
void GPIO_S_SET_INPUT(gpio_pin_t pin);
void GPIO_S_SET_PUSHPULL(gpio_pin_t pin);
void GPIO_S_SET_OD(gpio_pin_t pin);
void GPIO_S_SET_PULLUP(gpio_pin_t pin);
void GPIO_S_SET_PULLDOWN(gpio_pin_t pin);
void GPIO_S_SET_NOPULL(gpio_pin_t pin);
void GPIO_S_SET_SPEED_LOW(gpio_pin_t pin);
void GPIO_S_SET_SPEED_MED(gpio_pin_t pin);
void GPIO_S_SET_SPEED_HIGH(gpio_pin_t pin);
void GPIO_S_SET_ANALOG(gpio_pin_t pin);
void GPIO_S_SET_AF(gpio_pin_t pin, uint8_t af);
uint32_t GPIO_S_TEST(gpio_pin_t pin);
