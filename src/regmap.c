#include "regmap.h"
#include <string.h>
#include <stddef.h>
#include "i2c-slave.h"

#define member_size(type, member) sizeof(((type *)0)->member)

#define REG_RTC_FIRST       (offsetof(struct regmap, rtc))
#define REG_RTC_LAST        (offsetof(struct regmap, rtc) + member_size(struct regmap, rtc) - 1)

union regmap_union {
    struct regmap regs;
    uint8_t data[sizeof(struct regmap)];
};

union regmap_union regmap;
union regmap_union regmap_snapshot;

union {
    struct {
        bool rtc : 1;
    } flags;
    uint32_t all_flags;
} is_changed_flags;

bool is_write_completed = 0;

void regmap_set_rtc_time(const struct rtc_time * tm)
{
    regmap.regs.rtc.seconds = tm->seconds;
    regmap.regs.rtc.minutes = tm->minutes;
    regmap.regs.rtc.hours = tm->hours;
    regmap.regs.rtc.weekdays = tm->weekdays;
    regmap.regs.rtc.days = tm->days;
    regmap.regs.rtc.months = tm->months;
    regmap.regs.rtc.years = tm->years;
}

void regmap_set_rtc_alarm(const struct rtc_alarm * alarm)
{
    regmap.regs.rtc.alarm_seconds = alarm->seconds;
    regmap.regs.rtc.alarm_minutes = alarm->minutes;
    regmap.regs.rtc.alarm_hours = alarm->hours;
    regmap.regs.rtc.alarm_days = alarm->days;

    if (alarm->enabled) {
        regmap.regs.rtc.alarm_seconds |= 0x80;
    }
}

void regmap_set_adc_ch(enum adc_channel ch, uint16_t val)
{
    if (ch < ADC_CHANNEL_COUNT) {
        // regmap.regs.adc_channels[ch] = val;
    }
}

void regmap_set_iqr(uint8_t val)
{
    regmap.regs.irq.pwr_rise = val;
}

void regmap_make_snapshot(void)
{
    memcpy(regmap_snapshot.data, regmap.data, sizeof(struct regmap));
}

uint8_t regmap_get_snapshot_reg(uint8_t addr)
{
    if (addr > regmap_get_max_reg()) {
        return 0;
    }

    return regmap_snapshot.data[addr];
}

bool regmap_set_snapshot_reg(uint8_t addr, uint8_t value)
{
    if (addr > regmap_get_max_reg()) {
        return 0;
    }

    if (regmap_snapshot.data[addr] != value) {
        regmap_snapshot.data[addr] = value;

        i2c_slave_set_busy(1);

        if ((addr >= REG_RTC_FIRST) && (addr <= REG_RTC_LAST)) {
            is_changed_flags.flags.rtc = 1;
        }
    }

    return 1;
}

bool regmap_is_busy(void)
{
    return (is_changed_flags.all_flags != 0);
}

bool regmap_is_write_completed(void)
{
    bool ret = is_write_completed;
    is_write_completed = 0;
    return ret;
}

void regmap_set_write_completed(void)
{
    is_write_completed = 1;
}

bool regmap_is_rtc_changed(void)
{
    bool ret = is_changed_flags.flags.rtc;
    is_changed_flags.flags.rtc = 0;

    return ret;
}


void regmap_get_snapshop_rtc_time(struct rtc_time * tm)
{
    tm->seconds = regmap_snapshot.regs.rtc.seconds;
    tm->minutes = regmap_snapshot.regs.rtc.minutes;
    tm->hours = regmap_snapshot.regs.rtc.hours;
    tm->weekdays = regmap_snapshot.regs.rtc.weekdays;
    tm->days = regmap_snapshot.regs.rtc.days;
    tm->months = regmap_snapshot.regs.rtc.months;
    tm->years = regmap_snapshot.regs.rtc.years;
}
