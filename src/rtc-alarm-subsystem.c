#include "rtc.h"
#include "regmap-int.h"
#include "irq-subsystem.h"

/**
 * Модуль занимается:
 *  - перекладывает данные из RTC в regmap, при этом конвертирует их из BCD в обычный код
 *  - проверяет, записаны ли регистры RTC снаружи и переписывает их из regmap в RTC
 *  - выставляет флаг будильника в IRQ, если он сработал
 */

static bool alarm_enabled = 0;

bool rtc_alarm_is_alarm_enabled(void)
{
    return alarm_enabled;
}

void rtc_alarm_do_periodic_work(void)
{
    if (rtc_get_ready_read()) {
        struct rtc_time rtc_time;
        struct REGMAP_RTC_TIME regmap_time;
        rtc_get_datetime(&rtc_time);
        regmap_time.seconds = BCD_TO_BIN(rtc_time.seconds);
        regmap_time.minutes = BCD_TO_BIN(rtc_time.minutes);
        regmap_time.hours = BCD_TO_BIN(rtc_time.hours);
        regmap_time.days = BCD_TO_BIN(rtc_time.days);
        regmap_time.months = BCD_TO_BIN(rtc_time.months);
        regmap_time.years = BCD_TO_BIN(rtc_time.years);
        regmap_time.weekdays = rtc_time.weekdays;
        regmap_set_region_data(REGMAP_REGION_RTC_TIME, &regmap_time, sizeof(regmap_time));

        struct rtc_alarm rtc_alarm;
        struct REGMAP_RTC_ALARM regmap_alarm;
        rtc_get_alarm(&rtc_alarm);
        regmap_alarm.en = rtc_alarm.enabled;
        regmap_alarm.seconds = BCD_TO_BIN(rtc_alarm.seconds);
        regmap_alarm.minutes = BCD_TO_BIN(rtc_alarm.minutes);
        regmap_alarm.hours = BCD_TO_BIN(rtc_alarm.hours);
        regmap_alarm.days = BCD_TO_BIN(rtc_alarm.days);
        regmap_set_region_data(REGMAP_REGION_RTC_ALARM, &regmap_alarm, sizeof(regmap_alarm));

        struct REGMAP_RTC_CFG cfg;
        cfg.offset = rtc_get_offset();
        regmap_set_region_data(REGMAP_REGION_RTC_CFG, &cfg, sizeof(cfg));

        if (rtc_alarm.flag) {
            irq_set_flag(IRQ_ALARM);
            rtc_clear_alarm_flag();
            // После сработки будильника нужно его выключить
            // Чтобы он не срабатывал на втором круге
            rtc_alarm.enabled = 0;
            rtc_set_alarm(&rtc_alarm);
        }

        alarm_enabled = rtc_alarm.enabled;
    }

    if (regmap_is_region_changed(REGMAP_REGION_RTC_TIME)) {
        struct rtc_time rtc_time;
        struct REGMAP_RTC_TIME regmap_time;

        regmap_get_region_data(REGMAP_REGION_RTC_TIME, &regmap_time, sizeof(regmap_time));

        rtc_time.seconds = BIN_TO_BCD(regmap_time.seconds);
        rtc_time.minutes = BIN_TO_BCD(regmap_time.minutes);
        rtc_time.hours = BIN_TO_BCD(regmap_time.hours);
        rtc_time.days = BIN_TO_BCD(regmap_time.days);
        rtc_time.months = BIN_TO_BCD(regmap_time.months);
        rtc_time.years = BIN_TO_BCD(regmap_time.years);
        rtc_time.weekdays = regmap_time.weekdays;

        rtc_set_datetime(&rtc_time);

        regmap_clear_changed(REGMAP_REGION_RTC_TIME);
    }

    if (regmap_is_region_changed(REGMAP_REGION_RTC_ALARM)) {
        struct rtc_alarm rtc_alarm;
        struct REGMAP_RTC_ALARM regmap_alarm;

        regmap_get_region_data(REGMAP_REGION_RTC_ALARM, &regmap_alarm, sizeof(regmap_alarm));

        rtc_alarm.enabled = regmap_alarm.en;
        rtc_alarm.flag = 0;
        rtc_alarm.seconds = BIN_TO_BCD(regmap_alarm.seconds);
        rtc_alarm.minutes = BIN_TO_BCD(regmap_alarm.minutes);
        rtc_alarm.hours = BIN_TO_BCD(regmap_alarm.hours);
        rtc_alarm.days = BIN_TO_BCD(regmap_alarm.days);

        rtc_set_alarm(&rtc_alarm);
        regmap_clear_changed(REGMAP_REGION_RTC_ALARM);
    }

    if (regmap_is_region_changed(REGMAP_REGION_RTC_CFG)) {
        struct REGMAP_RTC_CFG cfg;
        regmap_get_region_data(REGMAP_REGION_RTC_ALARM, &cfg, sizeof(cfg));

        rtc_set_offset(cfg.offset);

        regmap_clear_changed(REGMAP_REGION_RTC_CFG);
    }
}

