#include "gpio-subsystem.h"
#include "config.h"
#include "gpio.h"
#include "regmap-int.h"

void gpio_init(void)
{
    GPIO_RESET(GPIO_VOUT_EN_PORT, GPIO_VOUT_EN_PIN);
    GPIO_SET_PUSHPULL(GPIO_VOUT_EN_PORT, GPIO_VOUT_EN_PIN);
    GPIO_SET_OUTPUT(GPIO_VOUT_EN_PORT, GPIO_VOUT_EN_PIN);
}

void gpio_do_periodic_work(void)
{
    if (regmap_is_region_changed(REGMAP_REGION_GPIO)) {
        struct REGMAP_GPIO g;
        regmap_get_region_data(REGMAP_REGION_GPIO, &g, sizeof(g));

        // TODO Check UVLO/OVP
        if (g.v_out) {
            GPIO_SET(GPIO_VOUT_EN_PORT, GPIO_VOUT_EN_PIN);
        } else {
            GPIO_RESET(GPIO_VOUT_EN_PORT, GPIO_VOUT_EN_PIN);
        }

        // TODO Get data form ADC
        g.a1 = 0;
        g.a2 = 0;
        g.a3 = 0;
        g.a4 = 0;

        regmap_set_region_data(REGMAP_REGION_GPIO, &g, sizeof(g));

        regmap_clear_changed(REGMAP_REGION_GPIO);
    }
}
