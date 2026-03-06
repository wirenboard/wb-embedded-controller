#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "systick.h"

// Мок для установки текущего системного времени
void utest_systick_set_time_ms(systime_t time_ms);

// Мок для увеличения времени на заданное количество мс
void utest_systick_advance_time_ms(systime_t delta_ms);

// Проверка вызова systick_init
bool utest_systick_was_init_called(void);
void utest_systick_reset_init_flag(void);
