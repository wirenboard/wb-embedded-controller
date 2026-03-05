#include "adc.h"
#include "utest_adc.h"
#include <string.h>

static int32_t adc_values_mv[ADC_CHANNEL_COUNT] = {0};

void utest_adc_set_ch_mv(enum adc_channel channel, int32_t mv)
{
    adc_values_mv[channel] = mv;
}

int32_t adc_get_ch_mv(enum adc_channel channel)
{
    return adc_values_mv[channel];
}

// Заглушки для неиспользуемых функций
void adc_init(enum adc_clock clock_divider, enum adc_vref vref) {}
void adc_set_lowpass_rc(enum adc_channel channel, uint16_t rc_ms) {}
void adc_set_offset_mv(enum adc_channel channel, int16_t offset_mv) {}
fix16_t adc_get_ch_adc_raw(enum adc_channel channel) { return 0; }
fix16_t adc_get_ch_mv_f16(enum adc_channel channel) { return 0; }
bool adc_get_ready(void) { return true; }
void adc_do_periodic_work(void) {}
