#include "temperature-control.h"
#include "config.h"
#include "adc.h"
#include "gpio.h"
#include "ntc.h"

#if defined EC_GPIO_HEATER
    static const gpio_pin_t heater_gpio = { EC_GPIO_HEATER };
    static const fix16_t heater_on_temp = F16(EC_HEATER_ON_TEMP);
    static const fix16_t heater_off_temp = F16(EC_HEATER_OFF_TEMP);

    struct heater_ctx {
        bool enabled;
        bool force_enabled;
    };

    static struct heater_ctx heater_ctx;

    static inline void heater_enable(void)
    {
        GPIO_S_SET(heater_gpio);
        heater_ctx.enabled = true;
    }

    static inline void heater_disable(void)
    {
        GPIO_S_RESET(heater_gpio);
        heater_ctx.enabled = false;
    }

#endif

static const fix16_t minimum_working_temperature = F16(WBEC_MINIMUM_WORKING_TEMPERATURE);

static inline fix16_t get_temperature(void)
{
    fix16_t ntc_raw = adc_get_ch_adc_raw(ADC_CHANNEL_ADC_NTC);
    fix16_t temperature = ntc_convert_adc_raw_to_temp(ntc_raw);

    return temperature;
}

void temperature_control_init(void)
{
    #if defined EC_GPIO_HEATER
        heater_disable();
        GPIO_S_SET_PUSHPULL(heater_gpio);
        GPIO_S_SET_OUTPUT(heater_gpio);
    #endif
}

void temperature_control_do_periodic_work(void)
{
    #if defined EC_GPIO_HEATER
        fix16_t temp = get_temperature();

        if (!heater_ctx.force_enabled) {
            if ((heater_ctx.enabled) && (temp > heater_off_temp)) {
                heater_disable();
            }
            if ((!heater_ctx.enabled) && (temp < heater_on_temp)) {
                heater_enable();
            }
        }
    #endif
}

void temperature_control_heater_force_control(bool force_enable)
{
    #if defined EC_GPIO_HEATER
        heater_ctx.force_enabled = force_enable;
        if (force_enable) {
            heater_enable();
        }
    #else
    (void)force_enable;
    #endif
}

bool temperature_control_is_temperature_ready(void)
{
    fix16_t temp = get_temperature();

    if (temp > minimum_working_temperature) {
        return true;
    }
    return false;
}

int16_t temperature_control_get_temperature_c_x100(void)
{
    fix16_t temp = get_temperature();

    int16_t temp_x100 = fix16_to_int(
        fix16_mul(temp, F16(100))
    );

    return temp_x100;
}
