#include "adc.h"
#include "utest_adc.h"
#include <string.h>

static int32_t adc_values_mv[ADC_CHANNEL_COUNT] = {0};
static fix16_t adc_values_raw[ADC_CHANNEL_COUNT] = {0};
static int16_t adc_offsets_mv[ADC_CHANNEL_COUNT] = {0};

void utest_adc_set_ch_mv(enum adc_channel channel, int32_t mv)
{
    adc_values_mv[channel] = mv;
}

void utest_adc_set_ch_raw(enum adc_channel channel, fix16_t raw_value)
{
    adc_values_raw[channel] = raw_value;
}

int32_t adc_get_ch_mv(enum adc_channel channel)
{
    return adc_values_mv[channel] + adc_offsets_mv[channel];
}

fix16_t adc_get_ch_adc_raw(enum adc_channel channel)
{
    return adc_values_raw[channel];
}

int16_t utest_adc_get_offset_mv(enum adc_channel channel)
{
    return adc_offsets_mv[channel];
}

void adc_set_offset_mv(enum adc_channel channel, int16_t offset_mv)
{
    adc_offsets_mv[channel] = offset_mv;
}

void adc_init(enum adc_clock clock_divider, enum adc_vref vref) {}
void adc_set_lowpass_rc(enum adc_channel channel, uint16_t rc_ms) {}
fix16_t adc_get_ch_mv_f16(enum adc_channel channel) { return 0; }
bool adc_get_ready(void) { return true; }
void adc_do_periodic_work(void) {}
