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

enum pwrkey_state {
    PWRKEY_UNINTIALIZATED,
    PWRKEY_RELEASED,
    PWRKEY_PRESSED,
};

enum press_state {
    PRESS_STATE_NO_PRESS,
    PRESS_STATE_PRESS_BEGIN,
    PRESS_STATE_LONG_PRESS,
};

static const gpio_pin_t pwrkey_gpio = { EC_GPIO_PWRKEY };

// Состояние gpio. Используется для подавления дребезга
struct pwrkey_gpio_ctx {
    systime_t timestamp;
    // bool prev_gpio_state;
    // bool logic_state;
    // bool initializated;
    enum pwrkey_state prev_gpio_state;
    enum pwrkey_state logic_state;
};

// Состояние кнопки после антидребезга. Используется для детектирования нажатий
struct pwrkey_logic_ctx {
    systime_t timestamp;
    enum pwrkey_state prev_logic_state;
    enum press_state press_state;
    bool short_pressed_flag;
    bool long_pressed_flag;
    // bool long_pressed_detected;
};

static struct pwrkey_gpio_ctx gpio_ctx;
static struct pwrkey_logic_ctx logic_ctx;

static inline enum pwrkey_state get_pwrkey_state(void)
{
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        if GPIO_S_TEST(pwrkey_gpio) {
            return PWRKEY_RELEASED;
        } else {
            return PWRKEY_PRESSED;
        }
    #elif EC_GPIO_PWRKEY_ACTIVE_HIGH
        if GPIO_S_TEST(pwrkey_gpio) {
            return PWRKEY_PRESSED;
        } else {
            return PWRKEY_RELEASED;
        }
    #endif
}

// Выполняет подавление дребезга, результат записывает в gpio_ctx.logic_state
static inline void debounce(enum pwrkey_state gpio_state)
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
static inline void handle_presses(enum pwrkey_state gpio_debounced_state)
{
    // Детектирование нажатий
    // Если состояние после дебаунса отличается от предыдущего, нужно распознавать  нажатия
    // При этом после включения питания сюда попадем в любом случае (нажата кнопка или отпущена)
    // после того как отработает дебаунс, т.к. исходное состояние - PWRKEY_UNINTIALIZATED
    if (logic_ctx.prev_logic_state != gpio_debounced_state) {
        // Нажатие кнопки - это смена состояния с PWRKEY_RELEASED на PWRKEY_PRESSED
        // Если предыдущее состояние было PWRKEY_UNINTIALIZATED, сюда не попадаем
        // Поэтому если исходно кнопка будет нажата (включи кнопкой и держат)
        // события нажатий не произойдут
        if ((logic_ctx.prev_logic_state == PWRKEY_RELEASED) &&
            (gpio_debounced_state == PWRKEY_PRESSED))
        {
            // If button pressed - save timestamp
            logic_ctx.timestamp = systick_get_system_time_ms();
            logic_ctx.press_state = PRESS_STATE_PRESS_BEGIN;
        }

        // Отпускание кнопки - это смена состояния с PWRKEY_PRESSED на PWRKEY_RELEASED
        // Тут нужно проверить, было ли начато нажание (PRESS_STATE_PRESS_BEGIN)
        // и если да - то какое оно: если не длинное - значит короткое
        if ((logic_ctx.prev_logic_state == PWRKEY_PRESSED) &&
            (gpio_debounced_state == PWRKEY_RELEASED))
        {
            if (logic_ctx.press_state == PRESS_STATE_PRESS_BEGIN) {
                logic_ctx.short_pressed_flag = 1;
            }
            logic_ctx.press_state = PRESS_STATE_NO_PRESS;
        }

        logic_ctx.prev_logic_state = gpio_debounced_state;
    }

    // Измеряем длительность удержания кнопки и фиксируем долгое нажатие
    // Дополнительно проверяем, что нажатие началось,
    // чтобы не фиксировать долгое нажатие, если кнопку держат после включения
    if ((logic_ctx.press_state == PRESS_STATE_PRESS_BEGIN) &&
        (gpio_debounced_state == PWRKEY_PRESSED))
    {
        systime_t held_time = systick_get_time_since_timestamp(logic_ctx.timestamp);
        if (held_time > PWRKEY_LONG_PRESS_TIME_MS) {
            logic_ctx.press_state = PRESS_STATE_LONG_PRESS;
            logic_ctx.long_pressed_flag = 1;
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
        #error "pwrkey polarity not defined"
    #endif

    // Set BUTTON pin as wakeup source
    PWR->CR3 |= (1 << (EC_GPIO_PWRKEY_WKUP_NUM - 1));
}

void pwrkey_do_periodic_work(void)
{
    enum pwrkey_state current_state = get_pwrkey_state();

    debounce(current_state);
    handle_presses(gpio_ctx.logic_state);
}

bool pwrkey_ready(void)
{
    return (gpio_ctx.logic_state != PWRKEY_UNINTIALIZATED);
}

bool pwrkey_pressed(void)
{
    return (gpio_ctx.logic_state == PWRKEY_PRESSED);
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
