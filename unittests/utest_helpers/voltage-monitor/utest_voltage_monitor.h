#pragma once
#include "voltage-monitor.h"
#include <stdbool.h>

// Сброс состояния voltage monitor
void utest_vmon_reset(void);

// Установка состояния канала voltage monitor
void utest_vmon_set_ch_status(enum vmon_channel ch, bool status);

// Установка состояния готовности voltage monitor
void utest_vmon_set_ready(bool ready);
