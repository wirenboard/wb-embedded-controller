#pragma once

#include <stdint.h>
#include <stddef.h>
#include "regmap-structs.h"
#include "rtc.h"
#include "adc.h"
#include <errno.h>

#define __REGMAP_REGION_NAME(addr, name, rw, members)              REGMAP_REGION_##name,

enum regmap_region {
    REGMAP(__REGMAP_REGION_NAME)

    REGMAP_REGION_COUNT
};

bool regmap_set_region_data(enum regmap_region r, const void * data, size_t size);
void regmap_get_region_data(enum regmap_region r, void * data, size_t size);
bool regmap_is_region_changed(enum regmap_region r);
void regmap_clear_changed(enum regmap_region r);

void regmap_ext_prepare_operation(uint16_t start_addr);
void regmap_ext_end_operation(void);
uint16_t regmap_ext_read_reg_autoinc(void);
void regmap_ext_write_reg_autoinc(uint16_t val);
