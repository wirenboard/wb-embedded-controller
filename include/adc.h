#pragma once
#include <stdint.h>
#include "fix16.h"
#include "config.h"

/* ADC channels names generation*/
#define ADC_ENUM(alias, ch_num, port, pin, rc_factor)           ADC_CHANNEL_##alias

enum adc_channel {
    ADC_CHANNELS_DESC(ADC_ENUM),
    ADC_CHANNEL_COUNT
};

void adc_init(void);
void adc_set_lowpass_rc(uint8_t channel, uint16_t rc_ms);
fix16_t adc_get_channel_raw_value(uint8_t channel);
