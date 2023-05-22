#pragma once
#include <stdint.h>
#include "fix16.h"
#include "config.h"

/* ADC channels names generation*/
#define ADC_ENUM(alias, ch_num, port, pin, rc_factor, k, offset_mv)     ADC_CHANNEL_##alias,

enum adc_channel {
    ADC_CHANNELS_DESC(ADC_ENUM)
    ADC_CHANNEL_COUNT
};

void adc_init(void);
void adc_set_lowpass_rc(enum adc_channel channel, uint16_t rc_ms);
fix16_t adc_get_ch_adc_raw(enum adc_channel channel);
uint32_t adc_get_ch_mv(enum adc_channel channel);
void adc_do_periodic_work(void);
