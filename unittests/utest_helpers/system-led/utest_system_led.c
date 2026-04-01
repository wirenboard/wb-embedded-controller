#include "system-led.h"
#include "utest_system_led.h"
#include "systick.h"
#include <stdbool.h>

/**
 * Mock для system-led модуля
 * Симулирует работу системного светодиода без использования реального GPIO
 */

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
static uint32_t periodic_work_call_count = 0;

// Вспомогательные функции
static inline void led_gpio_on(void)
{
    led_ctx.state = 1;
}

static inline void led_gpio_off(void)
{
    led_ctx.state = 0;
}

// Реализация функций из system-led.h
void system_led_init(void)
{
    led_gpio_off();
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
    periodic_work_call_count++;

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

// Функции для тестирования
enum utest_led_mode utest_system_led_get_mode(void)
{
    switch (led_ctx.mode) {
        case LED_OFF:
            return UTEST_LED_MODE_OFF;
        case LED_ON:
            return UTEST_LED_MODE_ON;
        case LED_BLINK:
            return UTEST_LED_MODE_BLINK;
        default:
            return UTEST_LED_MODE_OFF;
    }
}

void utest_system_led_get_blink_params(uint16_t *on_ms, uint16_t *off_ms)
{
    if (on_ms) {
        *on_ms = led_ctx.time[LED_TIME_ON];
    }
    if (off_ms) {
        *off_ms = led_ctx.time[LED_TIME_OFF];
    }
}

uint8_t utest_system_led_get_state(void)
{
    return led_ctx.state;
}

uint32_t utest_system_led_get_periodic_work_count(void)
{
    return periodic_work_call_count;
}

void utest_system_led_reset(void)
{
    led_ctx.mode = LED_OFF;
    led_ctx.timestamp = 0;
    led_ctx.state = 0;
    led_ctx.time[LED_TIME_ON] = 0;
    led_ctx.time[LED_TIME_OFF] = 0;
    periodic_work_call_count = 0;
}
