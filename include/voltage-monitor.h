#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

/* VMON channels names generation*/
#define __VMON_ENUM(vmon_name, adc_name, ok_min, ok_max, fail_min, fail_max)    VMON_CHANNEL_##vmon_name,

enum vmon_channel {
    VOLTAGE_MONITOR_DESC(__VMON_ENUM)
    VMON_CHANNEL_COUNT
};

void vmon_init(void);
bool vmon_ready(void);
bool vmon_get_ch_status(enum vmon_channel ch);
void vmon_do_periodic_work(void);
