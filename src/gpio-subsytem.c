#include "gpio-subsystem.h"
#include "config.h"
#include "gpio.h"
#include "regmap-int.h"
#include "linux-power-control.h"
#include "voltage-monitor.h"
#include "shared-gpio.h"
#include "bits.h"

/**
 * Модуль занимается работой с регионом GPIO в regmap
 *
 * Устанавливает состояния GPIO в regmap, если это входы
 * Управляет GPIO, если это выходы
 */

// Значения в регионе GPIO_AF (по 2 бита на пин)
enum gpio_regmap_af {
    GPIO_REGMAP_AF_GPIO = 0,
    GPIO_REGMAP_AF_UART = 1,
};

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
    uint16_t gpio_dir;
    uint16_t gpio_af;
};

static struct gpio_ctx gpio_ctx = {
    .gpio_ctrl = 0,
    .gpio_dir = outputs_only_gpios,
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
    if (gpio_ctx.gpio_dir & BIT(gpio)) {
        return true;
    } else {
        return false;
    }
}

static void set_mod_gpio_dir(uint16_t gpio_dir)
{
    gpio_dir &= ~inputs_only_gpios;
    gpio_dir |= outputs_only_gpios;

    for (unsigned mod = 0; mod < MOD_COUNT; mod++) {
        for (unsigned mod_gpio = 0; mod_gpio < MOD_GPIO_COUNT; mod_gpio++) {
            uint8_t mod_gpio_index = mod * MOD_GPIO_COUNT + mod_gpio;
            enum ec_ext_gpio global_gpio_index = mod_gpio_base[mod] + mod_gpio;
            // Можно менять режим работы пина MODx, только если он не используется как UART
            uint8_t af = (gpio_ctx.gpio_af >> (mod_gpio_index * 2)) & BIT_FIELD_MASK(2);
            if (af == GPIO_REGMAP_AF_GPIO) {
                enum mod_gpio_mode mode = MOD_GPIO_MODE_INPUT;
                if (gpio_is_output(global_gpio_index)) {
                    mode = MOD_GPIO_MODE_OUTPUT;
                }
                shared_gpio_set_mode(mod, mod_gpio, mode);
            } else {
                gpio_ctx.gpio_ctrl &= ~BIT(global_gpio_index);
            }
        }
    }

    gpio_ctx.gpio_dir = gpio_dir;
}

static void set_mod_gpio_af(uint16_t gpio_af)
{
    for (unsigned mod = 0; mod < MOD_COUNT; mod++) {
        for (unsigned mod_gpio = 0; mod_gpio < MOD_GPIO_COUNT; mod_gpio++) {
            enum ec_ext_gpio gpio = mod_gpio_base[mod] + mod_gpio;
            uint8_t af = (gpio_af >> ((mod * MOD_GPIO_COUNT + mod_gpio) * 2)) & BIT_FIELD_MASK(2);
            if (af == GPIO_REGMAP_AF_GPIO) {
                if (gpio_is_output(gpio)) {
                    shared_gpio_set_mode(mod, mod_gpio, MOD_GPIO_MODE_OUTPUT);
                } else {
                    shared_gpio_set_mode(mod, mod_gpio, MOD_GPIO_MODE_INPUT);
                }
            } else if (af == GPIO_REGMAP_AF_UART) {
                shared_gpio_set_mode(mod, mod_gpio, MOD_GPIO_MODE_AF_UART);
            }
        }
    }
    gpio_ctx.gpio_af = gpio_af;
}

static void control_v_out(void)
{
    bool v_in_is_in_proper_range = vmon_get_ch_status(VMON_CHANNEL_V_OUT);
    bool v_out_ctrl = get_gpio_ctrl(EC_EXT_GPIO_V_OUT);
    set_v_out_state(v_in_is_in_proper_range && v_out_ctrl);
}

static void set_gpio_values(void)
{
    // V_OUT нужно мониторить постоянно, т.к. его состояние зависит от входного напряжения
    control_v_out();

    for (unsigned mod = 0; mod < MOD_COUNT; mod++) {
        for (unsigned mod_gpio = 0; mod_gpio < MOD_GPIO_COUNT; mod_gpio++) {
            enum ec_ext_gpio gpio = mod_gpio_base[mod] + mod_gpio;
            if (shared_gpio_get_mode(mod, mod_gpio) == MOD_GPIO_MODE_OUTPUT) {
                shared_gpio_set_value(mod, mod_gpio, get_gpio_ctrl(gpio));
            }
        }
    }
}

static void collect_gpio_states(void)
{
    // Планировали сделать гистерезис на Analog Watchdog
    // вместо использования аппаратных внешних компараторов
    // gpio_ctx.a1 = 0;
    // gpio_ctx.a2 = 0;
    // gpio_ctx.a3 = 0;
    // gpio_ctx.a4 = 0;


    if (get_gpio_ctrl(EC_EXT_GPIO_V_OUT)) {
        gpio_ctx.gpio_state |= BIT(EC_EXT_GPIO_V_OUT);
    } else {
        gpio_ctx.gpio_state &= ~BIT(EC_EXT_GPIO_V_OUT);
    }

    for (unsigned mod = 0; mod < MOD_COUNT; mod++) {
        for (unsigned mod_gpio = 0; mod_gpio < MOD_GPIO_COUNT; mod_gpio++) {
            enum ec_ext_gpio gpio = mod_gpio_base[mod] + mod_gpio;
            bool state = false;
            if (shared_gpio_get_mode(mod, mod_gpio) == MOD_GPIO_MODE_INPUT) {
                state = shared_gpio_test(mod, mod_gpio);
            } else if (shared_gpio_get_mode(mod, mod_gpio) == MOD_GPIO_MODE_OUTPUT) {
                state = get_gpio_ctrl(gpio);
            }
            if (state) {
                gpio_ctx.gpio_state |= BIT(gpio);
            } else {
                gpio_ctx.gpio_state &= ~BIT(gpio);
            }
        }
    }
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
    gpio_ctx.gpio_dir = outputs_only_gpios;
    // Все пины по умолчанию на GPIO
    gpio_ctx.gpio_af = 0;

    set_mod_gpio_dir(gpio_ctx.gpio_dir);
    set_mod_gpio_af(gpio_ctx.gpio_af);

    regmap_set_region_data(REGMAP_REGION_GPIO_STATE, &gpio_ctx.gpio_state, sizeof(gpio_ctx.gpio_state));
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_ctx.gpio_dir, sizeof(gpio_ctx.gpio_dir));
    regmap_set_region_data(REGMAP_REGION_GPIO_AF, &gpio_ctx.gpio_af, sizeof(gpio_ctx.gpio_af));
}

void gpio_do_periodic_work(void)
{
    collect_gpio_states();
    set_gpio_values();

    struct REGMAP_GPIO_CTRL gpio_ctrl_regmap;
    if (regmap_get_data_if_region_changed(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl_regmap, sizeof(gpio_ctrl_regmap))) {
        gpio_ctx.gpio_ctrl = gpio_ctrl_regmap.gpio_ctrl;
    }

    struct REGMAP_GPIO_DIR gpio_dir_regmap;
    if (regmap_get_data_if_region_changed(REGMAP_REGION_GPIO_DIR, &gpio_dir_regmap, sizeof(gpio_dir_regmap))) {
        set_mod_gpio_dir(gpio_dir_regmap.gpio_dir);
    }

    struct REGMAP_GPIO_AF gpio_af_regmap;
    if (regmap_get_data_if_region_changed(REGMAP_REGION_GPIO_AF, &gpio_af_regmap, sizeof(gpio_af_regmap))) {
        set_mod_gpio_af(gpio_af_regmap.af);
    }

    regmap_set_region_data(REGMAP_REGION_GPIO_STATE, &gpio_ctx.gpio_state, sizeof(gpio_ctx.gpio_state));
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_ctx.gpio_dir, sizeof(gpio_ctx.gpio_dir));
}
