#pragma once
#include "config.h"

/* HW revision names generation*/
#define __HWREV_ENUM(name, code, res_up, res_down)          HWREV_##name = code,

enum hwrev {
    WBEC_HWREV_DESC(__HWREV_ENUM)

    HWREV_UNKNOWN = 0xFFFF
};

void hwrev_init(void);
enum hwrev hwrev_get(void);
uint16_t hwrev_get_adc_value(void);
