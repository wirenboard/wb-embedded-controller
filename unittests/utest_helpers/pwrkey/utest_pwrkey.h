#pragma once

#include "pwrkey.h"

#include <stdbool.h>
#include <stdint.h>

void utest_pwrkey_reset(void);
void utest_set_pwrkey_long_press(bool value);
void utest_set_pwrkey_pressed(bool value);
void utest_pwrkey_set_ready(bool ready);
void utest_pwrkey_set_short_press(bool value);
uint32_t utest_pwrkey_get_periodic_work_call_count(void);

// Опциональная модель антидребезга (по умолчанию ВЫКЛЮЧЕНА — совместимо со
// всеми существующими тестами). Когда включена, pwrkey_do_periodic_work()
// прогоняет тот же фильтр, что и реальный pwrkey.c: raw-состояние
// (utest_set_pwrkey_pressed) превращается в подтверждённое короткое нажатие
// (pwrkey_handle_short_press) только после удержания > PWRKEY_DEBOUNCE_MS и
// последующего отпускания > PWRKEY_DEBOUNCE_MS, по системному времени
// (utest_systick). Позволяет прогнать нажатие end-to-end через
// pwrkey_do_periodic_work, а не инъекцией на границе мока.
void utest_pwrkey_enable_debounce_model(bool on);
