#pragma once

#include <stdbool.h>

void utest_wbmz_set_powered_from_wbmz(bool powered);
void utest_set_wbmz_stepup_enabled(bool value);
uint32_t utest_get_wbmz_periodic_work_call_count(void);
uint32_t utest_get_wbmz_disable_stepup_call_count(void);
void utest_wbmz_common_reset(void);
