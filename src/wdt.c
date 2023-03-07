#include "wdt.h"
#include "systick.h"
#include "regmap.h"

struct wdt_ctx {
    uint8_t timeout_s;
    systime_t timestamp;
    bool run;
    bool timed_out;
};

static struct wdt_ctx wdt_ctx = {};

void wdt_set_timeout(uint8_t secs)
{
    if (secs == 0) {
        secs = 1;
    }
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
    if (wdt_ctx.run) {
        systime_t wdt_time_ms = systick_get_time_since_timestamp(wdt_ctx.timestamp);
        systime_t wdt_period_ms = wdt_ctx.timeout_s * 1000;
        if (wdt_time_ms > wdt_period_ms) {
            wdt_ctx.timed_out = 1;
            wdt_start_reset();
        }
    }

    if (regmap_snapshot_is_region_changed(REGMAP_REGION_WDT)) {
        struct REGMAP_WDT w;
        regmap_get_snapshop_region_data(REGMAP_REGION_WDT, &w, sizeof(w));

        wdt_set_timeout(w.timeout);
        if (w.run && !wdt_ctx.run) {
            wdt_start_reset();
        } else if (!w.run) {
            wdt_stop();
        }
        if (w.run && w.reset) {
            wdt_start_reset();
        }

        regmap_snapshot_clear_changed(REGMAP_REGION_WDT);
    }

    struct REGMAP_WDT w;
    w.reset = 0;
    w.run = wdt_ctx.run;
    w.timeout = wdt_ctx.timeout_s;
    regmap_set_region_data(REGMAP_REGION_WDT, &w, sizeof(w));
}
