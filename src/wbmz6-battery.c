#include "config.h"

#if defined WBEC_WBMZ6_SUPPORT
#include "axp221s.h"
#include "adc.h"

enum wbmz6_device {
    WBMZ6_DEVICE_NONE,
    WBMZ6_DEVICE_BATTERY,
    WBMZ6_DEVICE_SUPERCAP,
};

struct wbmz6_ctx {
    enum wbmz6_device device;
    uint16_t voltage_now_mv;
    int16_t current_now_ma;
    uint16_t capacity_percent;
    uint16_t temperature;

    uint16_t full_design_capacity_mah;
    uint16_t voltage_min_mv;
    uint16_t voltage_max_mv;
    uint16_t charge_current_ma;
};

static struct wbmz6_ctx wbmz6_ctx;

static enum wbmz6_device wmbz6_detect_device(void)
{
    enum wbmz6_device ret = WBMZ6_DEVICE_NONE;

    if (axp221s_is_present()) {
        ret = WBMZ6_DEVICE_BATTERY;
    } else {
        uint16_t supercap_mv = adc_get_ch_mv(ADC_CHANNEL_ADC_VBAT);
        if (supercap_mv > WBEC_WBMZ6_SUPERCAP_VOLTAGE_MIN_MV) {
            ret = WBMZ6_DEVICE_SUPERCAP;
        }
    }

    return ret;
}

void wbmz6_init(void)
{
    wbmz6_ctx.device = WBMZ6_DEVICE_NONE;
}

void wbmz6_do_periodic_work(void)
{
    enum wbmz6_device device_found = wmbz6_detect_device();

    if (device_found != wbmz6_ctx.device) {
        wbmz6_ctx.device = device_found;
        switch (device_found) {
        case WBMZ6_DEVICE_BATTERY:
            // Init battery

            wbmz6_ctx.full_design_capacity_mah = WBEC_WBMZ6_BATTERY_FULL_DESIGN_CAPACITY_MAH;
            wbmz6_ctx.voltage_min_mv = WBEC_WBMZ6_BATTERY_VOLTAGE_MIN_MV;
            wbmz6_ctx.voltage_max_mv = WBEC_WBMZ6_BATTERY_VOLTAGE_MAX_MV;
            wbmz6_ctx.charge_current_ma = WBEC_WBMZ6_BATTERY_CHARGE_CURRENT_MA;

            axp221s_init();
            axp221s_set_battery_full_desing_capacity(wbmz6_ctx.full_design_capacity_mah);
            axp221s_set_battery_voltage_min(wbmz6_ctx.voltage_min_mv);
            axp221s_set_battery_voltage_max(wbmz6_ctx.voltage_max_mv);
            axp221s_set_battery_charging_current_max(wbmz6_ctx.charge_current_ma);

            break;

        case WBMZ6_DEVICE_SUPERCAP:
            // Init supercap

            break;

        default:
            break;

        }
    }

}

#endif
