#pragma once
#include "wbmz6-status.h"

bool wbmz6_battery_is_present(void);
bool wbmz6_battery_init(void);
void wbmz6_battery_update_params(struct wbmz6_params *params);
void wbmz6_battery_update_status(struct wbmz6_status *status);
