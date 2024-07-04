#pragma once
#include "config.h"

/* HW revision names generation*/
#define __HWREV_ENUM(hwrev_name, hwrev_code, res_up, res_down)          HWREV_##hwrev_name,

enum hwrev {
    WBEC_HWREV_DESC(__HWREV_ENUM)

    HWREV_COUNT,
    HWREV_UNKNOWN = 0xFFFF
};

void hwrev_init(void);
enum hwrev hwrev_get(void);
uint16_t hwrev_get_code(void);
uint16_t hwrev_get_adc_value(void);
