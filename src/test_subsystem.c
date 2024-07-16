#include "regmap-structs.h"
#include "regmap-int.h"
#include "console.h"
#include "rtc.h"

/**
 * Выполняет разные тестовые функции, в обычной работе не участвует.
 *
 * - при установке флага send_test_message выводит сообщение в консоль
 * - бит enable_rtc_out включает/выключает выход 1Гц от RTC
 * - при установке флага reset_rtc_domain сбрасывает домен RTC
 */

static struct test_ctx {
    bool rtc_out_enabled;
} test_ctx = {};

void test_do_periodic_work(void)
{
    struct REGMAP_TEST test = {};
    if (regmap_get_data_if_region_changed(REGMAP_REGION_TEST, &test, sizeof(test))) {
        if (test.send_test_message) {
            console_print("\r\n");
            console_print_w_prefix("Test message\r\n\n");
            test.send_test_message = 0;
        }

        if ((test.enable_rtc_out) && (!test_ctx.rtc_out_enabled)) {
            rtc_enable_pa4_1hz_clkout();
            test_ctx.rtc_out_enabled = true;
        } else if ((!test.enable_rtc_out) && (test_ctx.rtc_out_enabled)) {
            rtc_disable_pa4_1hz_clkout();
            test_ctx.rtc_out_enabled = false;
        }

        if (test.reset_rtc) {
            rtc_reset();
            test.reset_rtc = 0;
        }
        regmap_set_region_data(REGMAP_REGION_TEST, &test, sizeof(test));
    }
}
