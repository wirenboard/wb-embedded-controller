#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "regmap-structs.h"

/**
 * Работа с regmap изнутри прошивки
 */

#define __REGMAP_REGION_NAME(addr, name, rw, members)              REGMAP_REGION_##name,

enum regmap_region {
    REGMAP(__REGMAP_REGION_NAME)

    REGMAP_REGION_COUNT
};

void regmap_init(void);
bool regmap_set_region_data(enum regmap_region r, const void * data, size_t size);
bool regmap_get_data_if_region_changed(enum regmap_region r, void * data, size_t size);
