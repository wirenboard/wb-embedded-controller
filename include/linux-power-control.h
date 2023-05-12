#pragma once
#include <stdbool.h>

void linux_pwr_init(bool on);
void linux_pwr_off(void);
void linux_pwr_reset(void);
void linux_pwr_hard_reset(void);
void linux_pwr_hard_off(void);
bool linux_pwr_is_busy(void);
void linux_pwr_do_periodic_work(void);
