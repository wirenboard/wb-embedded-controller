#pragma once
#include <stdint.h>

void wb_power_init(void);
void wb_power_on(void);
void wb_power_reset(void);
void wb_power_off_and_sleep(uint16_t off_delay);
void wb_power_do_periodic_work(void);
