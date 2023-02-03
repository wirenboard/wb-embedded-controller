#pragma once

#include <stdint.h>
#include "rtc.h"
#include "adc.h"

struct __attribute__((packed)) regmap {
    struct rtc {
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

    uint8_t res1[0x10 - 0x0B];  // 0B - 0F

    struct adc_ro {
        uint16_t v_in;          // 10
        uint16_t v_bat;         // 12
        uint16_t v_3_3;         // 14
        uint16_t v_5_0;         // 16
        uint16_t v_a1;          // 18
        uint16_t v_a2;          // 1A
        uint16_t v_a3;          // 1C
        uint16_t v_a4;          // 1E
        uint16_t temp;          // 20
    } adc_ro;

    uint8_t res2[0x26 - 0x22];  // 22 - 25

    struct adc_rw {
        uint8_t lowpass_rc_a1;  // 26
        uint8_t lowpass_rc_a2;  // 27
        uint8_t lowpass_rc_a3;  // 28
        uint8_t lowpass_rc_a4;  // 29
        uint8_t v_in_uvp;       // 2A
        uint8_t v_in_ovp;       // 2B
        uint8_t v_out_uvp;      // 2C
        uint8_t v_out_ovp;      // 2D
    } adc_rw;

    uint8_t res3[0x30 - 0x2E];  // 2E - 30

    struct gpio {
        bool a1:1;
        bool a2:1;
        bool a3:1;
        bool a4:1;
        bool v_out:1;
    } gpio;                     // 30

    struct irq {
        bool pwr_rise:1;
        bool pwr_fall:1;
        bool pwr_short_press:1;
        bool pwr_long_press:1;
        bool rtc_alarm:1;
        bool rtc_period:1;
    } irq,                      // 31
    irq_msk;                    // 32
};

void regmap_set_rtc_time(const struct rtc_time * tm);
void regmap_set_rtc_alarm(const struct rtc_alarm * alarm);
void regmap_set_adc_ch(enum adc_channel ch, uint16_t val);
void regmap_set_iqr(uint8_t val);


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
