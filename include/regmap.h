#pragma once

#include <stdint.h>
#include "rtc.h"

struct __attribute__((packed)) regmap {
    struct {
        uint8_t seconds;        // 00h Seconds
        uint8_t minutes;        // 01h Minutes
        uint8_t hours;          // 02h Hours
        uint8_t days;           // 03h Days
        uint8_t weekdays;       // 04h Weekdays
        uint8_t months;         // 05h Months
        uint8_t years;          // 06h Years

        uint8_t alarm_seconds;  // 07h Alarm: Seconds
        uint8_t alarm_minutes;  // 08h Alarm: Minutes
        uint8_t alarm_hours;    // 09h Alarm: Hours
        uint8_t alarm_days;     // 0Ah Alarm: Days
    } rtc;

    uint8_t res[5];             // 0B - 0F

    struct {
        uint16_t v_in;          // 10h V_IN
        uint16_t v_bat;         // 12h V_BAT
        uint16_t v_3v3;         // 14h V_3.3
        uint16_t v_5v0;         // 16h V_5.0
        uint16_t v_a1;          // 18h V_A1
        uint16_t v_a2;          // 1Ah V_A2
        uint16_t v_a3;          // 1Ch V_A3
        uint16_t v_a4;          // 1Eh V_A4
        uint16_t temp;          // 20h Temp
    } adc;
};

void regmap_set_rtc_time(const struct rtc_time * tm);
void regmap_set_rtc_alarm(const struct rtc_alarm * alarm);


void regmap_make_snapshot(void);
uint8_t regmap_get_snapshot_reg(uint8_t addr);
bool regmap_set_snapshot_reg(uint8_t addr, uint8_t value);


bool regmap_is_busy(void);
bool regmap_is_write_completed(void);
bool regmap_is_rtc_changed(void);

void regmap_set_write_completed(void);

void regmap_get_snapshop_rtc_time(struct rtc_time * tm);

static inline uint8_t regmap_get_max_reg(void)
{
    return sizeof(struct regmap) - 1;
}
