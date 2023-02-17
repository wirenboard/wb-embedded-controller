#pragma once
#include <stdbool.h>

void i2c_slave_init(void);
bool i2c_slave_is_busy(void);
void i2c_slave_set_free(void);
