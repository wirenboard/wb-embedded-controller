#pragma once

#include "pwrkey.h"

#include <stdbool.h>
#include <stdint.h>

void utest_pwrkey_reset(void);
void utest_set_pwrkey_long_press(bool value);
void utest_set_pwrkey_pressed(bool value);
void utest_pwrkey_set_ready(bool ready);
void utest_pwrkey_set_short_press(bool value);
uint32_t utest_pwrkey_get_periodic_work_call_count(void);
