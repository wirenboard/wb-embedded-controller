#pragma once
#include <stdint.h>
#include <stdbool.h>

void axp221s_init(void);
bool axp221s_is_1present(void);

void axp221s_set_battery_full_desing_capacity(uint16_t capacity_mah);
void axp221s_set_battery_charging_current_max(uint16_t current_ma);
void axp221s_set_battery_voltage_min(uint16_t voltage_mv);
void axp221s_set_battery_voltage_max(uint16_t voltage_mv);

uint16_t axp221s_get_battery_voltage_now(void);
int16_t axp221s_get_battery_current_now(void);
uint16_t axp221s_get_battery_capacity_percent(void);
uint16_t axp221s_get_battery_temperature(void);
