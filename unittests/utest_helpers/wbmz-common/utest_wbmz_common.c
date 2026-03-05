#include "wbmz-common.h"
#include "utest_wbmz_common.h"

static bool powered_from_wbmz = false;

void utest_wbmz_set_powered_from_wbmz(bool powered)
{
    powered_from_wbmz = powered;
}

bool wbmz_is_powered_from_wbmz(void)
{
    return powered_from_wbmz;
}

// Stubs for other wbmz functions
void wbmz_common_init(void) {}
void wbmz_common_do_periodic_work(void) {}
