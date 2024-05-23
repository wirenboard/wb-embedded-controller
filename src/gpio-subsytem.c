#include "gpio-subsystem.h"
#include "config.h"
#include "gpio.h"
#include "regmap-int.h"
#include "linux-power-control.h"
#include "voltage-monitor.h"

/**
 * Модуль занимается работой с регионом GPIO в regmap
 *
 * Устанавливает состояния GPIO в regmap, если это входы
 * Управляет GPIO, если это выходы
 */

static const gpio_pin_t v_out_gpio = { EC_GPIO_VOUT_EN };

static struct REGMAP_GPIO gpio_ctx = {};

static inline void set_v_out_state(bool state)
{
    if (state) {
        GPIO_S_SET(v_out_gpio);
    } else {
        GPIO_S_RESET(v_out_gpio);
    }
}

void gpio_init(void)
{
    // V_OUT управляет транистором, нужен выход push-pull
    set_v_out_state(0);
    GPIO_S_SET_PUSHPULL(v_out_gpio);
    GPIO_S_SET_OUTPUT(v_out_gpio);
}

void gpio_do_periodic_work(void)
{
    // TODO Get data from ADC
    // Планировали сделать гистерезис на Analog Watchdog
    // вместо использования аппаратных внешних компараторов
    gpio_ctx.a1 = 0;
    gpio_ctx.a2 = 0;
    gpio_ctx.a3 = 0;
    gpio_ctx.a4 = 0;

    set_v_out_state(gpio_ctx.v_out && vmon_get_ch_status(VMON_CHANNEL_V_OUT));

    regmap_set_region_data(REGMAP_REGION_GPIO, &gpio_ctx, sizeof(gpio_ctx));

    struct REGMAP_GPIO g;
    if (regmap_is_region_changed(REGMAP_REGION_GPIO, &g, sizeof(g))) {
        gpio_ctx.v_out = g.v_out;
    }
}
