#pragma once
#include <stdbool.h>

void utest_wbmz_reset(void);

void utest_wbmz_set_powered_from_wbmz(bool powered);
void utest_wbmz_set_vbat_ok(bool ok);

bool utest_wbmz_get_stepup_enabled(void);
