#pragma once
#include <stdint.h>
#include "fix16.h"

// Мок для установки значения напряжения на канале ADC
void utest_adc_set_ch_mv(enum adc_channel channel, int32_t mv);

// Мок для установки raw ADC значения на канале
void utest_adc_set_ch_raw(enum adc_channel channel, fix16_t raw_value);
