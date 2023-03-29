#include "pwrkey.h"
#include "config.h"
#include "gpio.h"
#include "systick.h"
#include "assert.h"

/**
 * Модуль ловит события с кнопки включения питания:
 *  - короткое нажатие
 *  - длинное нажатие
 *
 * Также занимается подавлением дребезга
 * и настраивает кнопку питания как источник пробуждения из standby
 *
 * На данный момент поддерживается только GPIOA!
 * Для других портов требуются доработки по части подтяжки в режиме standby
 */


static_assert(EC_GPIO_PWRKEY_WKUP_NUM >= 1 && EC_GPIO_PWRKEY_WKUP_NUM <= 6, "Unsupported WKUP number");

static const gpio_pin_t pwrkey_gpio = { EC_GPIO_PWRKEY };

// Состояние gpio. Используется для подавления дребезга
struct pwrkey_gpio_ctx {
    systime_t timestamp;
    bool prev_gpio_state;
    bool logic_state;
    bool initializated;
};

// Состояние кнопки после антидребезга. Используется для детектирования нажатий
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
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        return !GPIO_S_TEST(pwrkey_gpio);
    #elif EC_GPIO_PWRKEY_ACTIVE_HIGH
        return GPIO_S_TEST(pwrkey_gpio);
    #endif
}

// Выполняет подавление дребезга, результат записывает в gpio_ctx.logic_state
static inline void debounce(bool gpio_state)
{
    // If GPIO state changed - save timestamp
    if (gpio_ctx.prev_gpio_state != gpio_state) {
        gpio_ctx.prev_gpio_state = gpio_state;
        gpio_ctx.timestamp = systick_get_system_time_ms();
    }

    // If logic state and GPIO state differs - check debounce time elapsed
    if (gpio_ctx.logic_state != gpio_state) {
        systime_t held_time = systick_get_time_since_timestamp(gpio_ctx.timestamp);
        if (held_time > PWRKEY_DEBOUNCE_MS) {
            gpio_ctx.logic_state = gpio_state;
        }
    }
}

// Детектирует нажатия, результат - установленные флаги нажатий
// logic_ctx.short_pressed_flag или logic_ctx.long_pressed_flag
static inline void handle_presses(bool gpio_debounced_state)
{
    // Handle presses
    if (logic_ctx.prev_logic_state != gpio_debounced_state) {
        logic_ctx.prev_logic_state = gpio_debounced_state;
        if (gpio_debounced_state) {
            // If button pressed - save timestamp
            logic_ctx.timestamp = systick_get_system_time_ms();
            logic_ctx.long_pressed_detected = 0;
        } else {
            // If button released - check that it is not long press
            if (!logic_ctx.long_pressed_detected) {
                logic_ctx.short_pressed_flag = 1;
            }
        }
    }

    if (gpio_debounced_state) {
        systime_t held_time = systick_get_time_since_timestamp(logic_ctx.timestamp);
        if (held_time > PWRKEY_LONG_PRESS_TIME_MS) {
            logic_ctx.long_pressed_flag = 1;
            logic_ctx.long_pressed_detected = 1;
        }
    }
}

void pwrkey_init(void)
{
    GPIO_S_SET_INPUT(pwrkey_gpio);
    GPIO_S_SET_PULLUP(pwrkey_gpio);

    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        // Подтяжка вверх для кнопки питания в standby (кнопка замыкает вход на землю)
        PWR->PUCRA |= (1 << pwrkey_gpio.pin);
        // Set falling edge as wakeup trigger
        PWR->CR4 |= (1 << (EC_GPIO_PWRKEY_WKUP_NUM - 1));
    #elif EC_GPIO_PWRKEY_ACTIVE_HIGH
        PWR->PDCRA |= (1 << pwrkey_gpio.pin);
    #else
        #error "pwrkey polarity not defined
    #endif

    // Set BUTTON pin as wakeup source
    PWR->CR3 |= (1 << (EC_GPIO_PWRKEY_WKUP_NUM - 1));
}

void pwrkey_do_periodic_work(void)
{
    bool current_state = get_pwrkey_state();

    // После включения питания кнопка может быть нажата
    // (если кнопка и есть источник включения)
    // Поэтому прежде чем делать всё остальное, нужно подождать пока кнопку отпустят
    if (!gpio_ctx.initializated) {
        if (current_state) {
            gpio_ctx.timestamp = systick_get_system_time_ms();
        } else {
            if (systick_get_time_since_timestamp(gpio_ctx.timestamp) > PWRKEY_DEBOUNCE_MS) {
                gpio_ctx.initializated = 1;
            }
        }
        return;
    }

    debounce(current_state);
    handle_presses(gpio_ctx.logic_state);
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
