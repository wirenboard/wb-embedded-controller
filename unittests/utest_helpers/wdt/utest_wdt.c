#include "utest_wdt.h"

#include <string.h>

static struct {
    uint16_t timeout;
    bool started;
    bool timed_out;
} wdt_state = {0};

void utest_wdt_reset(void)
{
    memset(&wdt_state, 0, sizeof(wdt_state));
}

void utest_wdt_set_timed_out(bool value)
{
    wdt_state.timed_out = value;
}

uint16_t utest_wdt_get_timeout(void)
{
    return wdt_state.timeout;
}

bool utest_wdt_get_started(void)
{
    return wdt_state.started;
}

void wdt_set_timeout(uint16_t secs)
{
    wdt_state.timeout = secs;
}

void wdt_start_reset(void)
{
    wdt_state.started = true;
}

void wdt_stop(void)
{
    wdt_state.started = false;
}

bool wdt_handle_timed_out(void)
{
    bool ret = wdt_state.timed_out;
    wdt_state.timed_out = false;
    return ret;
}

void wdt_do_periodic_work(void) {}
