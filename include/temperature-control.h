#pragma once
#include <stdbool.h>
#include "fix16.h"

void temperature_control_init(void);
void temperature_control_do_periodic_work(void);

// Показывает, что текущая температура выше минимально допустимой и можно запускаться
bool temperature_control_is_temperature_ready(void);

int16_t temperature_control_get_temperature_c_x100(void);

// Принудительно включает нагреватель (для тестирования)
// При force_enable = false нагреватель управляется по температуре
void temperature_control_heater_force_control(bool force_enable);

