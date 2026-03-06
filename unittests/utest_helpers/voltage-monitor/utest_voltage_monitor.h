#pragma once
#include "voltage-monitor.h"
#include <stdbool.h>

// Mock for resetting voltage monitor state
void utest_vmon_reset(void);

// Mock for setting voltage monitor channel status
void utest_vmon_set_ch_status(enum vmon_channel ch, bool status);

// Mock for setting voltage monitor ready state
void utest_vmon_set_ready(bool ready);
