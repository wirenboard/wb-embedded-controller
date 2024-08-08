#pragma once
#include "wbmz6-status.h"
#include <stdbool.h>

bool wbmz6_supercap_is_present(void);
void wbmz6_supercap_init(void);
void wbmz6_supercap_update_params(struct wbmz6_params *params);
void wbmz6_supercap_update_status(struct wbmz6_status *status);
