#include "wbmz-common.h"
#include "utest_wbmz_common.h"

static bool powered_from_wbmz = false;
static bool wbmz_stepup_enabled = false;
static bool wbmz_vbat_ok = true;
static uint32_t wbmz_periodic_work_call_count = 0;

void utest_wbmz_common_reset(void)
{
    powered_from_wbmz = false;
    wbmz_stepup_enabled = false;
    wbmz_vbat_ok = true;
    wbmz_periodic_work_call_count = 0;
}

void utest_wbmz_set_powered_from_wbmz(bool powered)
{
    powered_from_wbmz = powered;
}

void utest_set_wbmz_stepup_enabled(bool value)
{
    wbmz_stepup_enabled = value;
}

void utest_wbmz_set_vbat_ok(bool value)
{
    wbmz_vbat_ok = value;
}

bool utest_wbmz_get_stepup_enabled(void)
{
    return wbmz_stepup_enabled;
}

uint32_t utest_get_wbmz_periodic_work_call_count(void)
{
    return wbmz_periodic_work_call_count;
}

bool wbmz_is_powered_from_wbmz(void)
{
    return powered_from_wbmz;
}

void wbmz_disable_stepup(void)
{
    wbmz_stepup_enabled = false;
}

void wbmz_enable_stepup(void)
{
    wbmz_stepup_enabled = true;
}

bool wbmz_is_stepup_enabled(void)
{
    return wbmz_stepup_enabled;
}

bool wbmz_is_vbat_ok(void)
{
    return wbmz_vbat_ok;
}

void wbmz_do_periodic_work(void)
{
    wbmz_periodic_work_call_count++;
}

void wbmz_common_init(void) {}
void wbmz_common_do_periodic_work(void) {}
