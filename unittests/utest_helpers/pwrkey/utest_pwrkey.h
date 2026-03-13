#pragma once

#include "pwrkey.h"

#include <stdbool.h>
#include <stdint.h>

void utest_pwrkey_reset(void);
void utest_linux_power_control_set_pwrkey_long_press(bool value);
void utest_linux_power_control_set_pwrkey_pressed(bool value);
uint32_t utest_linux_power_control_get_pwrkey_periodic_work_call_count(void);
