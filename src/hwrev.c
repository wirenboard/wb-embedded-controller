#include "hwrev.h"
#include "adc.h"
#include "fix16.h"
#include "array_size.h"

#define HWREV_ADC_VALUE_CENTER(res_up, res_down) \
    ((res_down) * 4096 / ((res_up) + (res_down)))

#define HWREV_ADC_VALUE_MIN(res_up, res_down) \
    HWREV_ADC_VALUE_CENTER(res_up, res_down) - \
    (HWREV_ADC_VALUE_CENTER(res_up, res_down) * WBEC_HWREV_DIFF_PERCENT / 100) - \
    WBEC_HWREV_DIFF_ADC

#define HWREV_ADC_VALUE_MAX(res_up, res_down) \
    HWREV_ADC_VALUE_CENTER(res_up, res_down) + \
    (HWREV_ADC_VALUE_CENTER(res_up, res_down) * WBEC_HWREV_DIFF_PERCENT / 100) + \
    WBEC_HWREV_DIFF_ADC

#define __HWREV_DATA(name, code, res_up, res_down) \
    { \
        .adc_min = HWREV_ADC_VALUE_MIN(res_up, res_down), \
        .adc_max = HWREV_ADC_VALUE_MAX(res_up, res_down), \
    },

struct hwrev_desc {
    int16_t adc_min;
    int16_t adc_max;
};

static const struct hwrev_desc hwrev_desc[] = {
    WBEC_HWREV_DESC(__HWREV_DATA)
};

static uint16_t hwrev_adc_value = 0;
enum hwrev hwrev = HWREV_UNKNOWN;

void hwrev_init(void)
{
    const unsigned hwrev_count = ARRAY_SIZE(hwrev_desc);
    hwrev_adc_value = fix16_to_int(adc_get_ch_adc_raw(ADC_CHANNEL_ADC_HW_VER));

    for (int i = 0; i < hwrev_count; i++) {
        if (hwrev_adc_value >= hwrev_desc[i].adc_min &&
            hwrev_adc_value <= hwrev_desc[i].adc_max) {
            hwrev = i;
            break;
        }
    }
}

enum hwrev hwrev_get(void)
{
    return hwrev;
}

uint16_t hwrev_get_adc_value(void)
{
    return hwrev_adc_value;
}

