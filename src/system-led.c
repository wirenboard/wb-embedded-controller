#include "system-led.h"
#include "config.h"
#include "gpio.h"
#include "systick.h"
#include <stdbool.h>

/**
 * Модуль управляет ситемным светодиодом
 *
 * Есть несколько режимов:
 *  - выключен
 *  - включен
 *  - мигание с заданным временем паузы/свечения
 */

static const gpio_pin_t system_led_gpio = { EC_GPIO_LED };

enum led_mode {
    LED_OFF,
    LED_ON,
    LED_BLINK,
};

struct led_ctx {
    enum led_mode mode;
    systime_t timestamp;
    bool state;
    uint16_t on_time_ms;
    uint16_t off_time_ms;
};

static struct led_ctx led_ctx = {};

static inline void led_gpio_on(void)
{
    #ifdef EC_GPIO_LED_ACTIVE_HIGH
        GPIO_S_SET(system_led_gpio);
    #else
        GPIO_S_RESET(system_led_gpio);
    #endif
    led_ctx.state = 1;
}

static inline void led_gpio_off(void)
{
    #ifdef EC_GPIO_LED_ACTIVE_HIGH
        GPIO_S_RESET(system_led_gpio);
    #else
        GPIO_S_SET(system_led_gpio);
    #endif
    led_ctx.state = 0;
}

void system_led_init(void)
{
    led_gpio_off();
    GPIO_S_SET_PUSHPULL(system_led_gpio);
    GPIO_S_SET_OUTPUT(system_led_gpio);
}

void system_led_disable(void)
{
    led_ctx.mode = LED_OFF;
    if (led_ctx.state) {
        led_gpio_off();
    }
}

void system_led_enable(void)
{
    led_ctx.mode = LED_ON;
    if (!led_ctx.state) {
        led_gpio_on();
    }
}

void system_led_blink(uint16_t on_ms, uint16_t off_ms)
{
    led_ctx.mode = LED_BLINK;
    led_ctx.on_time_ms = on_ms;
    led_ctx.off_time_ms = off_ms;
    led_ctx.timestamp = systick_get_system_time();
}

void system_led_do_periodic_work(void)
{
    if (led_ctx.mode != LED_BLINK) {
        return;
    }

    systime_t delay = led_ctx.state ? led_ctx.off_time_ms : led_ctx.on_time_ms;
    if (systick_get_time_since_timestamp(led_ctx.timestamp) < delay) {
        return;
    }

    if (led_ctx.state) {
        led_gpio_off();
    } else {
        led_gpio_on();
    }
    led_ctx.timestamp = systick_get_system_time();
}
