#pragma once
#include <stdint.h>
#include "fix16.h"
#include "adc.h"

// Мок для установки значения напряжения на канале ADC
void utest_adc_set_ch_mv(enum adc_channel channel, int32_t mv);

// Мок для установки raw ADC значения на канале
void utest_adc_set_ch_raw(enum adc_channel channel, fix16_t raw_value);

// Получить последнее установленное смещение канала АЦП в мВ
int16_t utest_adc_get_offset_mv(enum adc_channel channel);

// Сбросить мок: значения и смещения каналов в 0, АЦП готов, VDDA = ADC_VREF_EXT_MV
void utest_adc_reset(void);

// Мок для adc_get_ready(): по умолчанию АЦП готов
void utest_adc_set_ready(bool ready);

// Мок для adc_get_vdda_mv(): питание АЦП, измеренное по VREFINT
void utest_adc_set_vdda_mv(int32_t mv);

// Сколько раз для канала вызван adc_reset_lowpass()
uint32_t utest_adc_get_reset_lowpass_count(enum adc_channel channel);
