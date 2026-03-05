#include "utest_wdt_stm32.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Внутреннее состояние мока
static struct {
    bool initialized;
    bool reloaded;
    uint32_t reload_count;
    watchdog_reload_callback_t reload_callback;
} watchdog_state = {0};

// Реализация функций из wdt-stm32.h
void watchdog_init(void)
{
    watchdog_state.initialized = true;
    watchdog_state.reloaded = false;
    watchdog_state.reload_count = 0;
}

void watchdog_reload(void)
{
    watchdog_state.reloaded = true;
    watchdog_state.reload_count++;

    // Вызываем callback, если установлен
    if (watchdog_state.reload_callback != NULL) {
        watchdog_state.reload_callback();
    }
}

// Функции для тестирования
bool utest_watchdog_is_initialized(void)
{
    return watchdog_state.initialized;
}

bool utest_watchdog_is_reloaded(void)
{
    return watchdog_state.reloaded;
}

uint32_t utest_watchdog_get_reload_count(void)
{
    return watchdog_state.reload_count;
}

void utest_watchdog_set_reload_callback(watchdog_reload_callback_t callback)
{
    watchdog_state.reload_callback = callback;
}

void utest_watchdog_reset(void)
{
    watchdog_state.initialized = false;
    watchdog_state.reloaded = false;
    watchdog_state.reload_count = 0;
    watchdog_state.reload_callback = NULL;
}
