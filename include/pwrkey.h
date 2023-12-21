#pragma once
#include <stdbool.h>
#include <stdint.h>

void pwrkey_init(void);
bool pwrkey_ready(void);
bool pwrkey_pressed(void);
void pwrkey_do_periodic_work(void);
bool pwrkey_handle_short_press(void);
bool pwrkey_handle_long_press(void);
void pwrkey_set_debounce_ms(uint16_t debounce_ms);
