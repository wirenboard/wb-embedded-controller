#pragma once
#include <stdint.h>
#include <stdbool.h>

void wdt_set_timeout(uint16_t secs);
void wdt_start_reset(void);
void wdt_stop(void);
bool wdt_handle_timed_out(void);
void wdt_do_periodic_work(void);
