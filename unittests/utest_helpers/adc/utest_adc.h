#pragma once
#include <stdint.h>

// Мок для установки значения напряжения на канале ADC
void utest_adc_set_ch_mv(enum adc_channel channel, int32_t mv);
