#pragma once
#include <stdint.h>
#include <stdbool.h>

// Тип callback функции, вызываемой при watchdog_reload
typedef void (*watchdog_reload_callback_t)(void);

// Проверить, был ли инициализирован watchdog
bool utest_watchdog_is_initialized(void);

// Проверить, был ли выполнен reload watchdog
bool utest_watchdog_is_reloaded(void);

// Получить количество вызовов reload
uint32_t utest_watchdog_get_reload_count(void);

// Установить callback, вызываемый при каждом watchdog_reload
void utest_watchdog_set_reload_callback(watchdog_reload_callback_t callback);

// Сбросить состояние мока
void utest_watchdog_reset(void);

void utest_wdt_reset(void);
void utest_wdt_set_timed_out(bool value);
uint16_t utest_wdt_get_timeout(void);
bool utest_wdt_get_started(void);
