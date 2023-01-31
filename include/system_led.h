#pragma once
#include "gpio.h"

#define SYSTEM_LED_PORT     GPIOB
#define SYSTEM_LED_PIN      1

static inline void system_led_init(void)
{
    GPIO_RESET(SYSTEM_LED_PORT, SYSTEM_LED_PIN);
    GPIO_SET_OUTPUT(SYSTEM_LED_PORT, SYSTEM_LED_PIN);
    GPIO_SET_PUSHPULL(SYSTEM_LED_PORT, SYSTEM_LED_PIN);
}

static inline void system_led_on(void)
{
    GPIO_SET(SYSTEM_LED_PORT, SYSTEM_LED_PIN);
}

static inline void system_led_off(void)
{
    GPIO_RESET(SYSTEM_LED_PORT, SYSTEM_LED_PIN);
}
