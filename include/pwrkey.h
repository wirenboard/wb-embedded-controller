#pragma once
#include <stdbool.h>

void pwrkey_init(void);
void pwrkey_do_periodic_work(void);
bool pwrkey_handle_short_press(void);
bool pwrkey_handle_long_press(void);
