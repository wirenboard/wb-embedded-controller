#include "wdt.h"
#include "config.h"
#include "systick.h"
#include "regmap-int.h"

/**
 * Модуль реализует watchdog через regmap
 *
 * Позволяет:
 *  - задавать таймаут в секундах (как из прошивки, так и через regmap)
 *  - запускать и останавливать watchdog из прошивки
 *  - сбрасывать watchdog через regmap
 *  - ловить событие срабатывания watchdog
 *
 * Работает через системное время, не использует аппаратных ресурсов
 */

struct wdt_ctx {
    uint16_t timeout_s;
    systime_t timestamp;
    bool run;
    bool timed_out;
    bool fed;
};

static struct wdt_ctx wdt_ctx = {
    .timeout_s = WBEC_WATCHDOG_INITIAL_TIMEOUT_S,
};

void wdt_set_timeout(uint16_t secs)
{
    if (secs == 0) {
        secs = 1;
    } else if (secs > WBEC_WATCHDOG_MAX_TIMEOUT_S) {
        secs = WBEC_WATCHDOG_MAX_TIMEOUT_S;
    }
    wdt_ctx.timeout_s = secs;
}

void wdt_start_reset(void)
{
    wdt_ctx.timestamp = systick_get_system_time_ms();
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

// Возвращает true, если Linux сбрасывал watchdog через regmap
// с момента предыдущего вызова. Признак того, что система жива.
bool wdt_handle_fed(void)
{
    bool ret = wdt_ctx.fed;
    if (ret) {
        wdt_ctx.fed = 0;
    }
    return ret;
}

#ifdef __unittest_env__
    #include <string.h>

    void utest_wdt_module_reset_state(void)
    {
        memset(&wdt_ctx, 0, sizeof(wdt_ctx));
        wdt_ctx.timeout_s = WBEC_WATCHDOG_INITIAL_TIMEOUT_S;
    }
#endif

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

    struct REGMAP_WDT w;
    if (regmap_get_data_if_region_changed(REGMAP_REGION_WDT, &w, sizeof(w))) {
        if (w.timeout != wdt_ctx.timeout_s) {
            wdt_set_timeout(w.timeout);
            // После установки нового таймаута нужно сбросить watchdog,
            // т.к. в случае изменения таймаута на меньший возможно ложное срабатывание,
            // если операции установки таймаута и сброса будут разными посылками в regmap
            wdt_start_reset();
        }
        if (w.reset) {
            wdt_start_reset();
            wdt_ctx.fed = 1;
        }
    }

    w.reset = 0;
    w.timeout = wdt_ctx.timeout_s;
    regmap_set_region_data(REGMAP_REGION_WDT, &w, sizeof(w));
}
