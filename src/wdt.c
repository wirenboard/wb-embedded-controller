#include "wdt.h"
#include "systick.h"

struct wdt_ctx {
    uint8_t timeout_s;
    systime_t timestamp;
    bool run;
    bool timed_out;
};

static struct wdt_ctx wdt_ctx = {};

void wdt_set_timeout(uint8_t secs)
{
    wdt_ctx.timeout_s = secs;
}

void wdt_start_reset(void)
{
    wdt_ctx.timestamp = systick_get_system_time();
    wdt_ctx.run = 1;
}

void wdt_stop(void)
{
    wdt_ctx.run = 0;
}

bool wdt_handle_timed_out(void)
{
    bool ret = wdt_ctx.timed_out;
    if (ret) {
        wdt_ctx.timed_out = 0;
    }
    return ret;
}

void wdt_do_periodic_work(void)
{
    if (!wdt_ctx.run) {
        return;
    }
    systime_t wdt_time_ms = systick_get_time_since_timestamp(wdt_ctx.timestamp);
    systime_t wdt_period_ms = wdt_ctx.timeout_s * 1000;
    if (wdt_time_ms > wdt_period_ms) {
        wdt_ctx.timed_out = 1;
        wdt_start_reset();
    }
}
