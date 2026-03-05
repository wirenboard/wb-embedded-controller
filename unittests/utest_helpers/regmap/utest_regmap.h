#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "regmap-int.h"

// Mock functions for testing regmap
void utest_regmap_reset(void);
void utest_regmap_mark_region_changed(enum regmap_region r);
bool utest_regmap_get_region_data(enum regmap_region r, void * data, size_t size);
