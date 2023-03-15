#pragma once

#include <stdint.h>
#include <stddef.h>
#include "regmap-structs.h"
#include "rtc.h"
#include "adc.h"
#include <errno.h>

#define __REGMAP_REGION_NAME(name, addr, rw, members)              REGMAP_REGION_##name,

enum regmap_region {
    REGMAP(__REGMAP_REGION_NAME)

    REGMAP_REGION_COUNT
};

/**
 * @brief Sets data to regmap
 * 
 * @param r 
 * @param data 
 * @param size 
 * @return true 
 * @return false 
 */
bool regmap_set_region_data(enum regmap_region r, const void * data, size_t size);

void regmap_make_snapshot(void);
uint16_t regmap_get_snapshot_reg(uint16_t addr);
void regmap_get_snapshop_region_data(enum regmap_region r, void * data, size_t size);


int regmap_snapshot_set_reg(uint16_t addr, uint16_t value);
bool regmap_snapshot_is_region_changed(enum regmap_region r);
void regmap_snapshot_clear_changed(enum regmap_region r);
void regmap_snapshot_set_write_complited(void);
