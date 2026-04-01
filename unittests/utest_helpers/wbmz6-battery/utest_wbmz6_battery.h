#pragma once

#include "config.h"

#if defined WBEC_WBMZ6_SUPPORT

#include <stdbool.h>
#include <stdint.h>
#include "wbmz6-status.h"

// Управление состоянием батареи
void utest_wbmz6_battery_set_present(bool present);
void utest_wbmz6_battery_set_init_result(bool success);
void utest_wbmz6_battery_set_status(const struct wbmz6_status *status);
void utest_wbmz6_battery_set_params(const struct wbmz6_params *params);

// Проверка вызовов
bool utest_wbmz6_battery_was_init_called(void);

// Получение параметров, переданных при инициализации
const struct wbmz6_params* utest_wbmz6_battery_get_init_params(void);

// Сброс состояния мока
void utest_wbmz6_battery_reset(void);

#endif // WBEC_WBMZ6_SUPPORT
