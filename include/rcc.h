#pragma once

extern uint32_t SystemCoreClock;

void rcc_set_hsi_1mhz_low_power_run(void);
void rcc_set_hsi_pll_64mhz_clock(void);
