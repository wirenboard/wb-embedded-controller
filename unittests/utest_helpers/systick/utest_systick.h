#pragma once
#include <stdint.h>

// Мок для установки текущего системного времени
void utest_systick_set_time_ms(systime_t time_ms);

// Мок для увеличения времени на заданное количество мс
void utest_systick_advance_time_ms(systime_t delta_ms);
