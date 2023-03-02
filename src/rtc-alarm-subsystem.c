#include "rtc.h"
#include "regmap.h"
#include "irq-subsystem.h"

void rtc_alarm_do_periodic_work(void)
{
    if (rtc_get_ready_read()) {
        struct rtc_time rtc_time;
        struct rtc_alarm rtc_alarm;
        struct REGMAP_RTC_TIME regmap_time;
        struct REGMAP_RTC_ALARM regmap_alarm;
        rtc_get_datetime(&rtc_time);
        rtc_get_alarm(&rtc_alarm);

        regmap_time.seconds = BCD_TO_BIN(rtc_time.seconds);
        regmap_time.minutes = BCD_TO_BIN(rtc_time.minutes);
        regmap_time.hours = BCD_TO_BIN(rtc_time.hours);
        regmap_time.days = BCD_TO_BIN(rtc_time.days);
        regmap_time.months = BCD_TO_BIN(rtc_time.months);
        regmap_time.years = BCD_TO_BIN(rtc_time.years);
        regmap_time.weekdays = rtc_time.weekdays;

        regmap_alarm.en = rtc_alarm.enabled;
        regmap_alarm.seconds = BCD_TO_BIN(rtc_alarm.seconds);
        regmap_alarm.minutes = BCD_TO_BIN(rtc_alarm.minutes);
        regmap_alarm.hours = BCD_TO_BIN(rtc_alarm.hours);
        regmap_alarm.days = BCD_TO_BIN(rtc_alarm.days);

        regmap_set_region_data(REGMAP_REGION_RTC_TIME, &regmap_time, sizeof(regmap_time));
        regmap_set_region_data(REGMAP_REGION_RTC_ALARM, &regmap_alarm, sizeof(regmap_alarm));

        if (rtc_alarm.flag) {
            irq_set_flag(IRQ_ALARM);
            rtc_clear_alarm_flag();
        }
    }

    if (regmap_snapshot_is_region_changed(REGMAP_REGION_RTC_TIME)) {
        struct rtc_time rtc_time;
        struct REGMAP_RTC_TIME regmap_time;

        regmap_get_snapshop_region_data(REGMAP_REGION_RTC_TIME, &regmap_time, sizeof(regmap_time));

        rtc_time.seconds = BIN_TO_BCD(regmap_time.seconds);
        rtc_time.minutes = BIN_TO_BCD(regmap_time.minutes);
        rtc_time.hours = BIN_TO_BCD(regmap_time.hours);
        rtc_time.days = BIN_TO_BCD(regmap_time.days);
        rtc_time.months = BIN_TO_BCD(regmap_time.months);
        rtc_time.years = BIN_TO_BCD(regmap_time.years);
        rtc_time.weekdays = regmap_time.weekdays;

        rtc_set_datetime(&rtc_time);

        regmap_snapshot_clear_changed(REGMAP_REGION_RTC_TIME);
    }

    if (regmap_snapshot_is_region_changed(REGMAP_REGION_RTC_ALARM)) {
        struct rtc_alarm rtc_alarm;
        struct REGMAP_RTC_ALARM regmap_alarm;

        regmap_get_snapshop_region_data(REGMAP_REGION_RTC_ALARM, &regmap_alarm, sizeof(regmap_alarm));

        rtc_alarm.enabled = regmap_alarm.en;
        rtc_alarm.seconds = BIN_TO_BCD(regmap_alarm.seconds);
        rtc_alarm.minutes = BIN_TO_BCD(regmap_alarm.minutes);
        rtc_alarm.hours = BIN_TO_BCD(regmap_alarm.hours);
        rtc_alarm.days = BIN_TO_BCD(regmap_alarm.days);

        rtc_set_alarm(&rtc_alarm);
    }

    if (regmap_snapshot_is_region_changed(REGMAP_REGION_RTC_CFG)) {

    }
}

