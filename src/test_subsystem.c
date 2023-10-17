#include "regmap-structs.h"
#include "regmap-int.h"
#include "console.h"

/**
 * Выполняет разные тестовые функции, в обычной работе не участвует.
 *
 * - при установке флага send_test_message выводит сообщение в консоль
 */
void test_do_periodic_work(void)
{
    struct REGMAP_TEST test = {};
    if (regmap_is_region_changed(REGMAP_REGION_TEST)) {
        regmap_get_region_data(REGMAP_REGION_TEST, &test, sizeof(test));

        if (test.send_test_message) {
            console_print("\r\n");
            console_print_w_prefix("Test message\r\n\n");
            test.send_test_message = 0;
        }

        regmap_clear_changed(REGMAP_REGION_TEST);
        regmap_set_region_data(REGMAP_REGION_TEST, &test, sizeof(test));
    }
}
