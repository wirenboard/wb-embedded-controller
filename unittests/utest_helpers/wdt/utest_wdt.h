#pragma once

#include <stdbool.h>
#include <stdint.h>

void utest_wdt_reset(void);
void utest_wdt_set_timed_out(bool value);
uint16_t utest_wdt_get_timeout(void);
bool utest_wdt_get_started(void);
