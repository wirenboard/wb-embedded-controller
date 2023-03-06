#include "pwrkey.h"
#include "config.h"
#include "gpio.h"
#include "systick.h"

struct pwrkey_gpio_ctx {
    systime_t timestamp;
    bool prev_gpio_state;
    bool logic_state;
};

struct pwrkey_logic_ctx {
    systime_t timestamp;
    bool prev_logic_state;
    bool short_pressed_flag;
    bool long_pressed_flag;
    bool long_pressed_detected;
};

static struct pwrkey_gpio_ctx gpio_ctx;
static struct pwrkey_logic_ctx logic_ctx;

static inline bool get_pwrkey_state(void)
{
    return !GPIO_TEST(PWR_KEY_PORT, PWR_KEY_PIN);
}

void pwrkey_init(void)
{
    GPIO_SET_INPUT(PWR_KEY_PORT, PWR_KEY_PIN);
    GPIO_SET_PULLUP(PWR_KEY_PORT, PWR_KEY_PIN);
}

void pwrkey_do_periodic_work(void)
{
    bool current_state = get_pwrkey_state();

    // If GPIO state changed - save timestamp
    if (gpio_ctx.prev_gpio_state != current_state) {
        gpio_ctx.prev_gpio_state = current_state;
        gpio_ctx.timestamp = systick_get_system_time();
    }

    // If logic state and GPIO state differs - check debounce time elapsed
    if (gpio_ctx.logic_state != current_state) {
        systime_t held_time = systick_get_time_since_timestamp(gpio_ctx.timestamp);
        if (held_time > PWRKEY_DEBOUNCE_MS) {
            gpio_ctx.logic_state = current_state;
        }
    }

    // Handle presses
    if (logic_ctx.prev_logic_state != gpio_ctx.logic_state) {
        logic_ctx.prev_logic_state = gpio_ctx.logic_state;
        if (gpio_ctx.logic_state) {
            // If button pressed - save timestamp
            logic_ctx.timestamp = systick_get_system_time();
            logic_ctx.long_pressed_detected = 0;
        } else {
            // If button released - check that it is not long press
            if (!logic_ctx.long_pressed_detected) {
                logic_ctx.short_pressed_flag = 1;
            }
        }
    }

    if (gpio_ctx.logic_state) {
        systime_t held_time = systick_get_time_since_timestamp(logic_ctx.timestamp);
        if (held_time > PWRKEY_LONG_PRESS_TIME_MS) {
            logic_ctx.long_pressed_flag = 1;
            logic_ctx.long_pressed_detected = 1;
        }
    }
}

bool pwrkey_handle_short_press(void)
{
    bool ret = logic_ctx.short_pressed_flag;
    if (ret) {
        logic_ctx.short_pressed_flag = 0;
    }
    return ret;
}

bool pwrkey_handle_long_press(void)
{
    bool ret = logic_ctx.long_pressed_flag;
    if (ret) {
        logic_ctx.long_pressed_flag = 0;
    }
    return ret;
}
