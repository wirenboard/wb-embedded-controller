#include "gpio-subsystem.h"
#include "config.h"
#include "gpio.h"
#include "regmap-int.h"
#include "linux-power-control.h"
#include "voltage-monitor.h"
#include "shared-gpio.h"

/**
 * Модуль занимается работой с регионом GPIO в regmap
 *
 * Устанавливает состояния GPIO в regmap, если это входы
 * Управляет GPIO, если это выходы
 */

#define BIT(x)              (1 << (x))

enum ec_ext_gpio {
    EC_EXT_GPIO_A1,
    EC_EXT_GPIO_A2,
    EC_EXT_GPIO_A3,
    EC_EXT_GPIO_A4,
    EC_EXT_GPIO_V_OUT,
    // Порядок TX, RX, RTS должен совпадать с порядком в enum mod_gpio shared-gpio.h
    EC_EXT_GPIO_MOD1_TX,
    EC_EXT_GPIO_MOD1_RX,
    EC_EXT_GPIO_MOD1_RTS,
    EC_EXT_GPIO_MOD2_TX,
    EC_EXT_GPIO_MOD2_RX,
    EC_EXT_GPIO_MOD2_RTS,

    EC_EXT_GPIO_COUNT
};

static const uint16_t inputs_only_gpios = (
    BIT(EC_EXT_GPIO_A1) |
    BIT(EC_EXT_GPIO_A2) |
    BIT(EC_EXT_GPIO_A3) |
    BIT(EC_EXT_GPIO_A4)
);

static const uint16_t outputs_only_gpios = (
    BIT(EC_EXT_GPIO_V_OUT)
);

static const enum ec_ext_gpio mod_gpio_base[MOD_COUNT] = {
    [MOD1] = EC_EXT_GPIO_MOD1_TX,
    [MOD2] = EC_EXT_GPIO_MOD2_TX
};

static const gpio_pin_t v_out_gpio = { EC_GPIO_VOUT_EN };

struct gpio_ctx {
    uint16_t gpio_state;
    uint16_t gpio_ctrl;
    uint16_t gpio_mode;
};

static struct gpio_ctx gpio_ctx = {
    .gpio_ctrl = 0,
    .gpio_mode = outputs_only_gpios
};

static inline void set_v_out_state(bool state)
{
    if (state) {
        GPIO_S_SET(v_out_gpio);
    } else {
        GPIO_S_RESET(v_out_gpio);
    }
}

static inline bool get_gpio_ctrl(enum ec_ext_gpio gpio)
{
    if (gpio_ctx.gpio_ctrl & BIT(gpio)) {
        return true;
    } else {
        return false;
    }
}

static inline bool gpio_is_output(enum ec_ext_gpio gpio)
{
    if (gpio_ctx.gpio_mode & BIT(gpio)) {
        return true;
    } else {
        return false;
    }
}

static void set_mod_gpio_modes(void)
{
    for (unsigned mod = 0; mod < MOD_COUNT; mod++) {
        for (unsigned mod_gpio = 0; mod_gpio < MOD_GPIO_COUNT; mod_gpio++) {
            enum ec_ext_gpio gpio = mod_gpio_base[mod] + mod_gpio;
            // Можно менять режим работы пина MODx, только если он не используется как UART
            if (shared_gpio_get_mode(mod, mod_gpio) != MOD_GPIO_MODE_AF_UART) {
                enum mod_gpio_mode mode = MOD_GPIO_MODE_INPUT;
                if (gpio_is_output(gpio)) {
                    mode = MOD_GPIO_MODE_OUTPUT;
                }
                shared_gpio_set_mode(mod, mod_gpio, mode);
            } else {
                gpio_ctx.gpio_ctrl &= ~BIT(gpio);
            }
        }
    }
}

static void set_mod_gpio_values(void)
{
    for (unsigned mod = 0; mod < MOD_COUNT; mod++) {
        for (unsigned mod_gpio = 0; mod_gpio < MOD_GPIO_COUNT; mod_gpio++) {
            enum ec_ext_gpio gpio = mod_gpio_base[mod] + mod_gpio;
            if (shared_gpio_get_mode(mod, mod_gpio) == MOD_GPIO_MODE_OUTPUT) {
                shared_gpio_set_value(mod, mod_gpio, get_gpio_ctrl(gpio));
            }
        }
    }
}

static void collect_mod_gpio_states(void)
{
    for (unsigned mod = 0; mod < MOD_COUNT; mod++) {
        for (unsigned mod_gpio = 0; mod_gpio < MOD_GPIO_COUNT; mod_gpio++) {
            enum ec_ext_gpio gpio = mod_gpio_base[mod] + mod_gpio;
            if (shared_gpio_get_mode(mod, mod_gpio) == MOD_GPIO_MODE_INPUT) {
                bool state = shared_gpio_test(mod, mod_gpio);
                if (state) {
                    gpio_ctx.gpio_ctrl |= BIT(gpio);
                } else {
                    gpio_ctx.gpio_ctrl &= ~BIT(gpio);
                }
            }
        }
    }
}

static void control_v_out(void)
{
    bool v_in_is_proper_range = vmon_get_ch_status(VMON_CHANNEL_V_OUT);
    bool v_out_ctrl = get_gpio_ctrl(EC_EXT_GPIO_V_OUT);
    set_v_out_state(v_in_is_proper_range && v_out_ctrl);
}

void gpio_init(void)
{
    // V_OUT управляет транзистором, нужен выход push-pull
    set_v_out_state(0);
    GPIO_S_SET_PUSHPULL(v_out_gpio);
    GPIO_S_SET_OUTPUT(v_out_gpio);

    shared_gpio_init();
}

// Надо вызывать при перезагрузке линукса - возвращает GPIO в исходное состояние
// Не сбрасывает V_OUT
void gpio_reset(void)
{
    // Зануляем всё кроме V_OUT - он не должен сбрасываться при перезагрузке
    gpio_ctx.gpio_ctrl &= BIT(EC_EXT_GPIO_V_OUT);
    // Все пины по умолчанию на вход (кроме V_OUT)
    gpio_ctx.gpio_mode = outputs_only_gpios;

    set_mod_gpio_modes();
}

void gpio_do_periodic_work(void)
{
    // TODO Get data from ADC
    // Планировали сделать гистерезис на Analog Watchdog
    // вместо использования аппаратных внешних компараторов
    // gpio_ctx.a1 = 0;
    // gpio_ctx.a2 = 0;
    // gpio_ctx.a3 = 0;
    // gpio_ctx.a4 = 0;
    collect_mod_gpio_states();

    // V_OUT нужно мониторить постоянно, т.к. его состояние зависит от входного напряжения
    control_v_out();

    set_mod_gpio_values();

    struct REGMAP_GPIO_CTRL gpio_ctrl_regmap;
    if (regmap_get_data_if_region_changed(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl_regmap, sizeof(gpio_ctrl_regmap))) {
        // Менять снаружи можно только состояния output пинов
        gpio_ctx.gpio_ctrl &= ~gpio_ctx.gpio_mode;
        gpio_ctx.gpio_ctrl |= gpio_ctx.gpio_mode & gpio_ctrl_regmap.gpio_ctrl;
    }

    struct REGMAP_GPIO_MODE gpio_mode_regmap;
    if (regmap_get_data_if_region_changed(REGMAP_REGION_GPIO_MODE, &gpio_mode_regmap, sizeof(gpio_mode_regmap))) {
        gpio_mode_regmap.gpio_mode &= ~inputs_only_gpios;
        gpio_mode_regmap.gpio_mode |= outputs_only_gpios;

        gpio_ctx.gpio_mode = gpio_mode_regmap.gpio_mode;

        set_mod_gpio_modes();
    }

    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctx.gpio_ctrl, sizeof(gpio_ctx.gpio_ctrl));
    regmap_set_region_data(REGMAP_REGION_GPIO_MODE, &gpio_ctx.gpio_mode, sizeof(gpio_ctx.gpio_mode));
}
