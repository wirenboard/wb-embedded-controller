#pragma once

#include <stdbool.h>
#include <stdint.h>

void utest_wbmz_set_powered_from_wbmz(bool powered);
void utest_wbmz_set_vbat_ok(bool value);
void utest_set_wbmz_stepup_enabled(bool value);
bool utest_wbmz_get_stepup_enabled(void);
uint32_t utest_get_wbmz_periodic_work_call_count(void);
void utest_wbmz_common_reset(void);
