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
void rtc_reset(void);
bool rtc_get_ready_read(void);
void rtc_get_datetime(struct rtc_time * tm);
void rtc_set_datetime(const struct rtc_time * tm);
void rtc_get_alarm(struct rtc_alarm * alarm);
void rtc_set_alarm(const struct rtc_alarm * alarm);
uint16_t rtc_get_offset(void);
void rtc_set_offset(uint16_t offeset);
void rtc_clear_alarm_flag(void);
void rtc_enable_pc13_1hz_clkout(void);
void rtc_disable_pc13_1hz_clkout(void);
void rtc_enable_pa4_1hz_clkout(void);
void rtc_disable_pa4_1hz_clkout(void);
void rtc_set_periodic_wakeup(uint16_t period_s);
void rtc_disable_periodic_wakeup(void);
void rtc_save_to_tamper_reg(uint8_t index, uint32_t data);
uint32_t rtc_get_tamper_reg(uint8_t index);
