#pragma once
#include <stdint.h>

/* Тип управления диодом (control)  */
enum led_control
{
    CONTROL_EC,     // Управляется с EC
    CONTROL_LINUX,  // Управляется с Linux-а
};

/* Режим работы (аналоги Linux-овым trigger)*/
enum led_mode
{
    MODE_OFF,    // Всегда выключен
    MODE_ON,     // Всегда включен
    MODE_NONE,   // Включен или выключен по команде
    MODE_TIMER,  // Мигает с заданными таймингами
    //TODO: добавить еще аналогов стандартных LINUX-тригеров (например GPIO)
};

/* Состояние светимости диода */
enum led_state
{
    STATE_OFF,      // Не светится
    STATE_ON,       // Светится
};

void system_led_init(void);
void system_led_init(void);
void system_led_disable(void);
void system_led_enable(void);
void system_led_blink(uint16_t on_ms, uint16_t off_ms);
void system_led_do_periodic_work(void);
void system_led_set_control_from_ec(void);
