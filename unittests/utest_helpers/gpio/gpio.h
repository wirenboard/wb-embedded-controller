#pragma once

#include <stdint.h>

#define GPIO_INSTANCE_COUNT         4

#define __IO volatile

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

// GPIO state query functions for testing
uint32_t utest_gpio_get_mode(const gpio_pin_t pin);
uint32_t utest_gpio_get_output_type(const gpio_pin_t pin);
uint32_t utest_gpio_get_output_state(const gpio_pin_t pin);
