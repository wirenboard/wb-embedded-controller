#pragma once

#include <stdint.h>
#include "rtc.h"
#include "adc.h"

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

    uint16_t adc_channels[ADC_CHANNEL_COUNT];      // 10h - 20h
};

void regmap_set_rtc_time(const struct rtc_time * tm);
void regmap_set_rtc_alarm(const struct rtc_alarm * alarm);
void regmap_set_adc_ch(enum adc_channel ch, uint16_t val);


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
