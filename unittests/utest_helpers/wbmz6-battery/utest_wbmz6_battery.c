#include "config.h"

#if defined WBEC_WBMZ6_SUPPORT

#include "utest_wbmz6_battery.h"
#include "wbmz6-battery.h"
#include <string.h>

// Состояние мока
static bool battery_present = false;
static bool init_result = true;
static struct wbmz6_status battery_status = {};
static struct wbmz6_params battery_params_to_return = {};
static struct wbmz6_params battery_params_received = {};

// Счетчики вызовов
static bool init_called = false;

void utest_wbmz6_battery_reset(void)
{
    battery_present = false;
    init_result = true;
    memset(&battery_status, 0, sizeof(battery_status));
    memset(&battery_params_to_return, 0, sizeof(battery_params_to_return));
    memset(&battery_params_received, 0, sizeof(battery_params_received));

    init_called = false;
}

void utest_wbmz6_battery_set_present(bool present)
{
    battery_present = present;
}

void utest_wbmz6_battery_set_init_result(bool success)
{
    init_result = success;
}

void utest_wbmz6_battery_set_status(const struct wbmz6_status *status)
{
    if (status) {
        memcpy(&battery_status, status, sizeof(battery_status));
    }
}

void utest_wbmz6_battery_set_params(const struct wbmz6_params *params)
{
    if (params) {
        memcpy(&battery_params_to_return, params, sizeof(battery_params_to_return));
    }
}

bool utest_wbmz6_battery_was_init_called(void)
{
    return init_called;
}

const struct wbmz6_params* utest_wbmz6_battery_get_init_params(void)
{
    return &battery_params_received;
}

// Реализация мокируемых функций
bool wbmz6_battery_is_present(void)
{
    return battery_present;
}

bool wbmz6_battery_init(struct wbmz6_params *params)
{
    init_called = true;

    if (params && init_result) {
        memcpy(params, &battery_params_to_return, sizeof(*params));
        memcpy(&battery_params_received, params, sizeof(*params));
    }

    return init_result;
}

void wbmz6_battery_update_status(struct wbmz6_status *status)
{
    if (status) {
        memcpy(status, &battery_status, sizeof(*status));
    }
}

#endif // WBEC_WBMZ6_SUPPORT
