#include "adc.h"
#include "utest_adc.h"
#include <string.h>

static int32_t adc_values_mv[ADC_CHANNEL_COUNT] = {0};
static fix16_t adc_values_raw[ADC_CHANNEL_COUNT] = {0};
static int16_t adc_offsets_mv[ADC_CHANNEL_COUNT] = {0};
static uint32_t adc_reset_lowpass_count[ADC_CHANNEL_COUNT] = {0};
static bool adc_ready = true;
static int32_t adc_vdda_mv = ADC_VREF_EXT_MV;

void utest_adc_reset(void)
{
    memset(adc_values_mv, 0, sizeof(adc_values_mv));
    memset(adc_values_raw, 0, sizeof(adc_values_raw));
    memset(adc_offsets_mv, 0, sizeof(adc_offsets_mv));
    memset(adc_reset_lowpass_count, 0, sizeof(adc_reset_lowpass_count));
    adc_ready = true;
    adc_vdda_mv = ADC_VREF_EXT_MV;
}

void utest_adc_set_ready(bool ready)
{
    adc_ready = ready;
}

void utest_adc_set_vdda_mv(int32_t mv)
{
    adc_vdda_mv = mv;
}

uint32_t utest_adc_get_reset_lowpass_count(enum adc_channel channel)
{
    return adc_reset_lowpass_count[channel];
}

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

fix16_t adc_get_ch_mv_f16(enum adc_channel channel)
{
    return fix16_from_int(adc_values_mv[channel] + adc_offsets_mv[channel]);
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

int32_t adc_get_vdda_mv(void)
{
    return adc_vdda_mv;
}

void adc_reset_lowpass(enum adc_channel channel)
{
    adc_reset_lowpass_count[channel]++;
}

void adc_init(enum adc_clock clock_divider, enum adc_vref vref) {}
void adc_set_lowpass_rc(enum adc_channel channel, uint16_t rc_ms) {}
bool adc_get_ready(void) { return adc_ready; }
void adc_do_periodic_work(void) {}
