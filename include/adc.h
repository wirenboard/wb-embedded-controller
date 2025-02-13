#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "fix16.h"
#include "config.h"

/* ADC channels names generation*/
#define ADC_ENUM(alias, ch_num, port, pin, rc_factor, k)     ADC_CHANNEL_##alias,

enum adc_channel {
    ADC_CHANNELS_DESC(ADC_ENUM)
    ADC_CHANNEL_COUNT
};

enum adc_clock {
    ADC_CLOCK_NO_DIV = 0,
    ADC_CLOCK_DIV_2 = 1,
    ADC_CLOCK_DIV_4 = 2,
    ADC_CLOCK_DIV_6 = 3,
    ADC_CLOCK_DIV_8 = 4,
    ADC_CLOCK_DIV_10 = 5,
    ADC_CLOCK_DIV_12 = 6,
    ADC_CLOCK_DIV_16 = 7,
    ADC_CLOCK_DIV_32 = 8,
    ADC_CLOCK_DIV_64 = 9,
    ADC_CLOCK_DIV_128 = 10,
    ADC_CLOCK_DIV_256 = 11,
};

enum adc_vref {
    ADC_VREF_EXT,
    ADC_VREF_INT,
};

void adc_init(enum adc_clock clock_divider, enum adc_vref vref);
void adc_set_lowpass_rc(enum adc_channel channel, uint16_t rc_ms);
void adc_set_offset_mv(enum adc_channel channel, int16_t offset_mv);
fix16_t adc_get_ch_adc_raw(enum adc_channel channel);
int32_t adc_get_ch_mv(enum adc_channel channel);
fix16_t adc_get_ch_mv_f16(enum adc_channel channel);
bool adc_get_ready(void);
void adc_do_periodic_work(void);
