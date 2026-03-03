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

#if defined EC_MOD1_MOD2_GPIO_CONTROL
    static const enum ec_ext_gpio mod_gpio_base[MOD_COUNT] = {
        [MOD1] = EC_EXT_GPIO_MOD1_TX,
        [MOD2] = EC_EXT_GPIO_MOD2_TX
    };
#endif

static const gpio_pin_t v_out_gpio = { EC_GPIO_VOUT_EN };

struct gpio_ctx {
    uint16_t gpio_ctrl_req_val;
    uint16_t gpio_ctrl_req_mask;
    uint16_t gpio_ctrl;
    uint16_t gpio_dir;
    uint16_t gpio_af;
};

static struct gpio_ctx gpio_ctx = {
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

static void set_mod_gpio_dir(uint16_t new_dir)
{
    new_dir &= ~inputs_only_gpios;
    new_dir |= outputs_only_gpios;

    #if defined EC_MOD1_MOD2_GPIO_CONTROL
        for (unsigned mod = 0; mod < MOD_COUNT; mod++) {
            for (unsigned mod_gpio = 0; mod_gpio < MOD_GPIO_COUNT; mod_gpio++) {
                enum ec_ext_gpio global_gpio_index = mod_gpio_base[mod] + mod_gpio;
                const uint16_t global_gpio_mask = BIT(global_gpio_index);

                // смотрим, поменялось ли направление на пине MODx
                // для этого сравниваем биты в new_dir и gpio_ctx.gpio_dir
                // и проверяем, изменился ли бит, соответствующий пину MODx (global_gpio_index)
                if ((new_dir ^ gpio_ctx.gpio_dir) & global_gpio_mask) {
                    uint8_t mod_gpio_index = mod * MOD_GPIO_COUNT + mod_gpio;
                    uint8_t af = (gpio_ctx.gpio_af >> (mod_gpio_index * 2)) & BIT_FIELD_MASK(2);
                    // Можно менять режим работы пина MODx, только если он не используется как UART
                    if (af == GPIO_REGMAP_AF_GPIO) {
                        enum mod_gpio_mode mode = MOD_GPIO_MODE_INPUT;
                        if (new_dir & global_gpio_mask) {
                            mode = MOD_GPIO_MODE_OUTPUT;
                            /**
                            * Линукс делает слудующие действия, если хочет изменить режим пина с входа на выход:
                            * 1) читает текущее состояние пина
                            *    1.1) если оно уже нужное - состоние не записывает
                            *    1.2) если нет - записывает в gpio_ctrl (при этом пин всё ещё вход)
                            * 2) меняет направление на выход
                            *
                            * В случае 1.2 будет проблема - между пунктами 1.2 и 2 состояние переопределится
                            * текущим состояние входа и записанное значение в 1.2 потеряется.
                            *
                            * Надо проверить, был ли запрос с линукса на установку значения
                            * этого пина перед сменой на OUTPUT.
                            * Если был - ставим значение оттуда, если нет - берем из регистра
                            */
                            bool value = gpio_ctx.gpio_ctrl & global_gpio_mask;
                            if (gpio_ctx.gpio_ctrl_req_mask & global_gpio_mask) {
                                // Был запрос с линукса - берем значение оттуда
                                value = gpio_ctx.gpio_ctrl_req_val & global_gpio_mask;
                                // Сбросим бит в маске запроса
                                gpio_ctx.gpio_ctrl_req_mask &= ~global_gpio_mask;
                                // Обновим бит в регистре
                                gpio_ctx.gpio_ctrl &= ~global_gpio_mask;
                                gpio_ctx.gpio_ctrl |= gpio_ctx.gpio_ctrl_req_val & global_gpio_mask;
                            }
                            shared_gpio_set_value(mod, mod_gpio, value);
                        }
                        shared_gpio_set_mode(mod, mod_gpio, mode);
                    } else {
                        gpio_ctx.gpio_ctrl &= ~global_gpio_mask;
                    }
                }
            }
        }
    #endif

    gpio_ctx.gpio_dir = new_dir;
}

static void set_mod_gpio_af(void)
{
    #if defined EC_MOD1_MOD2_GPIO_CONTROL
        for (unsigned mod = 0; mod < MOD_COUNT; mod++) {
            for (unsigned mod_gpio = 0; mod_gpio < MOD_GPIO_COUNT; mod_gpio++) {
                enum ec_ext_gpio gpio = mod_gpio_base[mod] + mod_gpio;
                uint8_t af = (gpio_ctx.gpio_af >> ((mod * MOD_GPIO_COUNT + mod_gpio) * 2)) & BIT_FIELD_MASK(2);
                if (af == GPIO_REGMAP_AF_GPIO) {
                    if (gpio_ctx.gpio_dir & BIT(gpio)) {
                        shared_gpio_set_mode(mod, mod_gpio, MOD_GPIO_MODE_OUTPUT);
                    } else {
                        shared_gpio_set_mode(mod, mod_gpio, MOD_GPIO_MODE_INPUT);
                    }
                } else if (af == GPIO_REGMAP_AF_UART) {
                    shared_gpio_set_mode(mod, mod_gpio, MOD_GPIO_MODE_AF_UART);
                }
            }
        }
    #endif
}

static void control_v_out(void)
{
    bool v_in_is_in_proper_range = vmon_get_ch_status(VMON_CHANNEL_V_OUT);
    bool v_out_ctrl = gpio_ctx.gpio_ctrl & BIT(EC_EXT_GPIO_V_OUT);
    set_v_out_state(v_in_is_in_proper_range && v_out_ctrl);
}

static void set_mod_gpio_values(uint16_t new_ctrl)
{
    gpio_ctx.gpio_ctrl_req_val = new_ctrl;
    gpio_ctx.gpio_ctrl_req_mask = new_ctrl ^ gpio_ctx.gpio_ctrl;
    gpio_ctx.gpio_ctrl = new_ctrl;

    #if defined EC_MOD1_MOD2_GPIO_CONTROL
        for (unsigned mod = 0; mod < MOD_COUNT; mod++) {
            for (unsigned mod_gpio = 0; mod_gpio < MOD_GPIO_COUNT; mod_gpio++) {
                enum ec_ext_gpio gpio = mod_gpio_base[mod] + mod_gpio;

                if (gpio_ctx.gpio_ctrl_req_mask & BIT(gpio)) {
                    const bool value = gpio_ctx.gpio_ctrl & BIT(gpio);
                    shared_gpio_set_value(mod, mod_gpio, value);
                }
            }
        }
    #endif
}

static void collect_gpio_states(void)
{
    // Планировали сделать гистерезис на Analog Watchdog
    // вместо использования аппаратных внешних компараторов
    gpio_ctx.gpio_ctrl &= ~(
        BIT(EC_EXT_GPIO_A1) |
        BIT(EC_EXT_GPIO_A2) |
        BIT(EC_EXT_GPIO_A3) |
        BIT(EC_EXT_GPIO_A4)
    );

    #if defined EC_MOD1_MOD2_GPIO_CONTROL
        for (unsigned mod = 0; mod < MOD_COUNT; mod++) {
            for (unsigned mod_gpio = 0; mod_gpio < MOD_GPIO_COUNT; mod_gpio++) {
                enum ec_ext_gpio gpio = mod_gpio_base[mod] + mod_gpio;
                bool state = false;
                if (shared_gpio_get_mode(mod, mod_gpio) == MOD_GPIO_MODE_INPUT) {
                    state = shared_gpio_test(mod, mod_gpio);
                    if (state) {
                        gpio_ctx.gpio_ctrl |= BIT(gpio);
                    } else {
                        gpio_ctx.gpio_ctrl &= ~BIT(gpio);
                    }
                }
            }
        }
    #endif
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
    // Все пины по умолчанию на GPIO
    gpio_ctx.gpio_af = 0;

    // Все пины по умолчанию на вход (кроме V_OUT)
    set_mod_gpio_dir(outputs_only_gpios);
    set_mod_gpio_af();

    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctx.gpio_ctrl, sizeof(gpio_ctx.gpio_ctrl));
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_ctx.gpio_dir, sizeof(gpio_ctx.gpio_dir));
    regmap_set_region_data(REGMAP_REGION_GPIO_AF, &gpio_ctx.gpio_af, sizeof(gpio_ctx.gpio_af));
}

void gpio_do_periodic_work(void)
{
    struct REGMAP_GPIO_CTRL gpio_ctrl_regmap;
    if (regmap_get_data_if_region_changed(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl_regmap, sizeof(gpio_ctrl_regmap))) {
        set_mod_gpio_values(gpio_ctrl_regmap.gpio_ctrl);
    }

    struct REGMAP_GPIO_DIR gpio_dir_regmap;
    if (regmap_get_data_if_region_changed(REGMAP_REGION_GPIO_DIR, &gpio_dir_regmap, sizeof(gpio_dir_regmap))) {
        set_mod_gpio_dir(gpio_dir_regmap.gpio_dir);
        regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_ctx.gpio_dir, sizeof(gpio_ctx.gpio_dir));
    }

    struct REGMAP_GPIO_AF gpio_af_regmap;
    if (regmap_get_data_if_region_changed(REGMAP_REGION_GPIO_AF, &gpio_af_regmap, sizeof(gpio_af_regmap))) {
        gpio_ctx.gpio_af = gpio_af_regmap.af;
        set_mod_gpio_af();
    }

    // V_OUT нужно мониторить постоянно, т.к. его состояние зависит от входного напряжения
    control_v_out();

    collect_gpio_states();

    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctx.gpio_ctrl, sizeof(gpio_ctx.gpio_ctrl));
}
