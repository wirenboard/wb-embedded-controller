#include "config.h"
#include "regmap-int.h"
#include "regmap-structs.h"
#include "wbmz-common.h"

#if defined WBEC_WBMZ6_SUPPORT

#include "wbmz6-status.h"
#include "wbmz6-battery.h"
#include "wbmz6-supercap.h"
#include "systick.h"

enum wbmz6_device {
    WBMZ6_DEVICE_NONE,
    WBMZ6_DEVICE_BATTERY,
    WBMZ6_DEVICE_SUPERCAP,
};

static enum wbmz6_device wbmz6_device = WBMZ6_DEVICE_NONE;
static struct wbmz6_params wbmz6_params = {};
static struct wbmz6_status wbmz6_status = {};
systime_t wbmz6_last_poll_time;

static enum wbmz6_device wmbz6_detect_device(void)
{
    if (wbmz6_battery_is_present()) {
        return WBMZ6_DEVICE_BATTERY;
    }

    if (wbmz6_supercap_is_present()) {
        wbmz6_supercap_init();
        wbmz6_battery_update_params(&wbmz6_params);
        return WBMZ6_DEVICE_SUPERCAP;
    }

    return WBMZ6_DEVICE_NONE;
}

static bool wbmz6_init_device(enum wbmz6_device device)
{
    switch (device) {
    case WBMZ6_DEVICE_BATTERY:
        if (wbmz6_battery_init()) {
            wbmz6_battery_update_params(&wbmz6_params);
            return true;
        }
        return false;

    case WBMZ6_DEVICE_SUPERCAP:
        wbmz6_supercap_init();
        wbmz6_supercap_update_params(&wbmz6_params);
        return true;

    default:
        return false;
    }
    return false;
}

static void wbmz6_poll_device(enum wbmz6_device device)
{
    switch (device) {
    case WBMZ6_DEVICE_BATTERY:
        wbmz6_battery_update_status(&wbmz6_status);
        break;

    case WBMZ6_DEVICE_SUPERCAP:
        wbmz6_supercap_update_status(&wbmz6_status);
        break;

    default:
        break;
    }
}

static void wbmz6_do_periodic_work(void)
{
    if (systick_get_time_since_timestamp(wbmz6_last_poll_time) < WBEC_WBMZ6_POLL_PERIOD_MS) {
        return;
    }
    wbmz6_last_poll_time = systick_get_system_time_ms();

    enum wbmz6_device device_found = wmbz6_detect_device();

    if (wbmz6_device != device_found) {
        wbmz6_device = device_found;

        if (!wbmz6_init_device(device_found)) {
            wbmz6_device = WBMZ6_DEVICE_NONE;
        }
    }

    wbmz6_poll_device(wbmz6_device);
}

#endif

void wbmz_subsystem_do_periodic_work(void)
{
    struct REGMAP_PWR_STATUS p = {};
    p.powered_from_wbmz = wbmz_is_powered_from_wbmz();
    p.wbmz_stepup_enabled = wbmz_is_stepup_enabled();

    #if defined WBEC_GPIO_WBMZ_CHARGE_ENABLE

        p.wbmz_charging_enabled = wbmz_is_charging_enabled();

    #endif

    #if defined WBEC_WBMZ6_SUPPORT

        wbmz6_do_periodic_work();

        if (wbmz6_device == WBMZ6_DEVICE_BATTERY) {
            p.wbmz_battery_present = 1;
        }
        if (wbmz6_device == WBMZ6_DEVICE_SUPERCAP) {
            p.wbmz_supercap_present = 1;
        }

        p.wbmz_full_design_capacity = wbmz6_params.full_design_capacity_mah;
        p.wbmz_voltage_min_design = wbmz6_params.voltage_min_mv;
        p.wbmz_voltage_max_design = wbmz6_params.voltage_max_mv;
        p.wbmz_constant_charge_current = wbmz6_params.charge_current_ma;

        p.wbmz_voltage_now = wbmz6_status.voltage_now_mv;
        p.wbmz_current_now = wbmz6_status.current_now_ma;
        p.wbmz_capacity_percent = wbmz6_status.capacity_percent;
        p.wbmz_temperature = wbmz6_status.temperature;

    #endif


    regmap_set_region_data(REGMAP_REGION_PWR_STATUS, &p, sizeof(p));
}

