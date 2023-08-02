#pragma once
#include <stdbool.h>

void linux_cpu_pwr_seq_init(bool on);
void linux_cpu_pwr_seq_on(void);
void linux_cpu_pwr_seq_hard_off(void);
void linux_cpu_pwr_seq_hard_reset(void);
void linux_cpu_pwr_seq_reset_pmic(void);
bool linux_cpu_pwr_seq_is_busy(void);
void linux_cpu_pwr_seq_do_periodic_work(void);
void linux_cpu_pwr_seq_enable_wbmz(void);
bool linux_cpu_pwr_seq_is_powered_from_wbmz(void);
