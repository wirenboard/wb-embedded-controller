#include "wbmz-common.h"
#include "utest_wbmz_common.h"

static struct {
    bool powered_from_wbmz;
    bool stepup_enabled;
    bool vbat_ok;
} wbmz_state = {0};

void utest_wbmz_reset(void)
{
    wbmz_state.powered_from_wbmz = false;
    wbmz_state.stepup_enabled = false;
    wbmz_state.vbat_ok = true;
}

void utest_wbmz_set_powered_from_wbmz(bool powered)
{
    wbmz_state.powered_from_wbmz = powered;
}

void utest_wbmz_set_vbat_ok(bool ok)
{
    wbmz_state.vbat_ok = ok;
}

bool utest_wbmz_get_stepup_enabled(void)
{
    return wbmz_state.stepup_enabled;
}

bool wbmz_is_powered_from_wbmz(void)
{
    return wbmz_state.powered_from_wbmz;
}

void wbmz_enable_stepup(void)
{
    wbmz_state.stepup_enabled = true;
}

void wbmz_disable_stepup(void)
{
    wbmz_state.stepup_enabled = false;
}

bool wbmz_is_stepup_enabled(void)
{
    return wbmz_state.stepup_enabled;
}

bool wbmz_is_vbat_ok(void)
{
    return wbmz_state.vbat_ok;
}

void wbmz_init(void) {}
void wbmz_do_periodic_work(void) {}
void wbmz_set_stepup_force_control(bool force_control, bool en)
{
    (void)force_control;
    (void)en;
}

bool wbmz_is_charging_enabled(void) { return false; }
void wbmz_set_charging_force_control(bool force_control, bool en)
{
    (void)force_control;
    (void)en;
}
