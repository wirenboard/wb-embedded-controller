#pragma once

#include <stdbool.h>

void utest_wbmz_set_powered_from_wbmz(bool powered);
void utest_linux_power_control_set_wbmz_stepup_enabled(bool value);
uint32_t utest_linux_power_control_get_wbmz_periodic_work_call_count(void);
uint32_t utest_linux_power_control_get_wbmz_disable_stepup_call_count(void);
void utest_wbmz_common_reset(void);
