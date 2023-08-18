#pragma once

#include <stdint.h>

void regmap_ext_prepare_operation(uint16_t start_addr);
void regmap_ext_end_operation(void);
uint16_t regmap_ext_read_reg_autoinc(void);
void regmap_ext_write_reg_autoinc(uint16_t val);
