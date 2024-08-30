#pragma once
#include <stdint.h>

void system_led_init(void);
void system_led_init(void);
void system_led_disable(void);
void system_led_enable(void);
void system_led_blink(uint16_t on_ms, uint16_t off_ms);
void system_led_do_periodic_work(void);
void system_led_set_control_from_ec(void);
