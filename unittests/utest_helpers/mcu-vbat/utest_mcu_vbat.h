#pragma once

#include <stdbool.h>
#include "mcu-vbat.h"

// Проверить, был ли вызван mcu_vbat_prepare_standby()
bool utest_mcu_vbat_was_prepare_standby_called(void);

// Сбросить состояние мока
void utest_mcu_vbat_reset(void);
