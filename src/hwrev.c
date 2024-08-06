#include "hwrev.h"
#include "config.h"
#include "adc.h"
#include "fix16.h"
#include "array_size.h"
#include "regmap-structs.h"
#include "regmap-int.h"
#include "wbmcu_system.h"
#include <string.h>

#define HWREV_ADC_VALUE_EXPECTED(res_up, res_down) \
    ((res_down) * 4096 / ((res_up) + (res_down)))

#define HWREV_ADC_VALUE_MIN(res_up, res_down) \
    HWREV_ADC_VALUE_EXPECTED(res_up, res_down) - \
    (HWREV_ADC_VALUE_EXPECTED(res_up, res_down) * WBEC_HWREV_DIFF_PERCENT / 100) - \
    WBEC_HWREV_DIFF_ADC

#define HWREV_ADC_VALUE_MAX(res_up, res_down) \
    HWREV_ADC_VALUE_EXPECTED(res_up, res_down) + \
    (HWREV_ADC_VALUE_EXPECTED(res_up, res_down) * WBEC_HWREV_DIFF_PERCENT / 100) + \
    WBEC_HWREV_DIFF_ADC

#define __HWREV_DATA(hwrev_name, hwrev_code, res_up, res_down) \
    { \
        .code = hwrev_code, \
        .adc_min = HWREV_ADC_VALUE_MIN(res_up, res_down), \
        .adc_max = HWREV_ADC_VALUE_MAX(res_up, res_down), \
    },

struct hwrev_desc {
    uint16_t code;
    int16_t adc_min;
    int16_t adc_max;
};

static const struct hwrev_desc hwrev_desc[HWREV_COUNT] = {
    WBEC_HWREV_DESC(__HWREV_DATA)
};

static enum hwrev hwrev = HWREV_UNKNOWN;
static uint16_t hwrev_code = HWREV_UNKNOWN;

void hwrev_init(void)
{
    int16_t hwrev_adc_value = fix16_to_int(adc_get_ch_adc_raw(ADC_CHANNEL_ADC_HW_VER));

    for (int i = 0; i < HWREV_COUNT; i++) {
        if (hwrev_adc_value >= hwrev_desc[i].adc_min &&
            hwrev_adc_value <= hwrev_desc[i].adc_max) {
            hwrev = i;
            hwrev_code = hwrev_desc[i].code;
            break;
        }
    }
}

enum hwrev hwrev_get(void)
{
    return hwrev;
}

void hwrev_put_hw_info_to_regmap(void)
{
    struct REGMAP_HW_INFO_PART1 hw_info_1 = {
        .wbec_id = WBEC_ID,
        .hwrev_code = 0,
        .hwrev_error_flag = 0,
        .fwrev = { FW_VERSION_NUMBERS },
    };
    struct REGMAP_HW_INFO_PART2 hw_info_2 = {};

    hw_info_1.hwrev_code = hwrev_code;
    memcpy(hw_info_2.uid, (uint8_t *)UID_BASE, sizeof(hw_info_2.uid));

    if (hwrev == WBEC_HWREV) {
        hw_info_2.hwrev_ok = WBEC_ID;
    } else {
        hw_info_1.hwrev_error_flag = 0b1010;
    }

    regmap_set_region_data(REGMAP_REGION_HW_INFO_PART1, &hw_info_1, sizeof(hw_info_1));
    regmap_set_region_data(REGMAP_REGION_HW_INFO_PART2, &hw_info_2, sizeof(hw_info_2));
}
