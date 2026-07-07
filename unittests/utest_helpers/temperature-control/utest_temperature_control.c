#include "temperature-control.h"
#include "utest_temperature_control.h"

// По умолчанию комнатная температура
static int16_t temperature_c_x100 = 2500;

void utest_temperature_control_set_temperature_c_x100(int16_t t_c_x100)
{
    temperature_c_x100 = t_c_x100;
}

int16_t temperature_control_get_temperature_c_x100(void)
{
    return temperature_c_x100;
}

void temperature_control_init(void) {}
void temperature_control_do_periodic_work(void) {}
bool temperature_control_is_temperature_ready(void) { return true; }
void temperature_control_heater_force_control(bool force_enable) { (void)force_enable; }
