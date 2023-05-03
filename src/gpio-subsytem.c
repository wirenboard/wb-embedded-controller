#include "gpio-subsystem.h"
#include "config.h"
#include "gpio.h"
#include "regmap-int.h"

/**
 * Модуль занимается работой с регионом GPIO в regmap
 *
 * Устанавливает состояния GPIO в regmap, если это входы
 * Управляет GPIO, если это выходы
 */

static const gpio_pin_t v_out_gpio = { EC_GPIO_VOUT_EN };
static const gpio_pin_t status_bat_gpio = { EC_GPIO_STATUS_BAT };

static struct REGMAP_GPIO gpio_ctx = {};

static inline void set_v_out_state(bool state)
{
    if (state) {
        GPIO_S_SET(v_out_gpio);
    } else {
        GPIO_S_RESET(v_out_gpio);
    }
}

static inline uint8_t get_status_bat_state(void)
{
    if (GPIO_S_TEST(status_bat_gpio)) {
        return 1;
    } else {
        return 0;
    }
}


void gpio_init(void)
{
    // V_OUT управляет транистором, нужен выход push-pull
    set_v_out_state(0);
    GPIO_S_SET_PUSHPULL(v_out_gpio);
    GPIO_S_SET_OUTPUT(v_out_gpio);

    // STATUS_BAT это вход, который WBMZ тянет к земле открытым коллектором
    // Подтянут снаружи к V_EC
    GPIO_S_SET_INPUT(status_bat_gpio);
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

    gpio_ctx.status_bat = get_status_bat_state();

    // TODO Check UVLO/OVP
    set_v_out_state(gpio_ctx.v_out);

    regmap_set_region_data(REGMAP_REGION_GPIO, &gpio_ctx, sizeof(gpio_ctx));

    if (regmap_is_region_changed(REGMAP_REGION_GPIO)) {
        struct REGMAP_GPIO g;
        regmap_get_region_data(REGMAP_REGION_GPIO, &g, sizeof(g));

        gpio_ctx.v_out = g.v_out;

        regmap_clear_changed(REGMAP_REGION_GPIO);
    }
}
