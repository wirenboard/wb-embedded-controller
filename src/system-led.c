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

enum led_time_settings {
    LED_TIME_OFF,
    LED_TIME_ON,

    LED_TIME_COUNT,
};

struct led_ctx {
    enum led_mode mode;
    systime_t timestamp;
    uint8_t state;
    uint16_t time[LED_TIME_COUNT];
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
    led_ctx.time[LED_TIME_ON] = on_ms;
    led_ctx.time[LED_TIME_OFF] = off_ms;
    led_ctx.timestamp = systick_get_system_time_ms();
}

void system_led_do_periodic_work(void)
{
    if (led_ctx.mode != LED_BLINK) {
        return;
    }

    // state принимает значения 0 или 1, используем его как индекс массива для получения
    // времени ожидания
    // Если светодиод сейчас выключен (state == 0), нужно получить время до включения (LED_TIME_ON == 0)
    systime_t delay = led_ctx.time[led_ctx.state];
    if (systick_get_time_since_timestamp(led_ctx.timestamp) < delay) {
        return;
    }

    if (led_ctx.state) {
        led_gpio_off();
    } else {
        led_gpio_on();
    }
    led_ctx.timestamp = systick_get_system_time_ms();
}
