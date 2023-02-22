#pragma once
#include <stdint.h>
#include <stdbool.h>

#define BCD_TO_BIN(x)           (((x) & 0x0f) + ((x) >> 4) * 10)
#define BIN_TO_BCD(x)           ((((x) / 10) << 4) + (x) % 10)

struct rtc_time {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t days;
    uint8_t weekdays;
    uint8_t months;
    uint8_t years;
};

struct rtc_alarm {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t days;

    bool enabled;
    bool flag;
};

void rtc_init(void);
bool rtc_get_ready_read(void);
void rtc_get_datetime(struct rtc_time * tm);
void rtc_set_datetime(const struct rtc_time * tm);
void rtc_get_alarm(struct rtc_alarm * alarm);
void rtc_set_alarm(const struct rtc_alarm * alarm);
void rtc_start_calibration(void);
void rtc_clear_alarm_flag(void);
