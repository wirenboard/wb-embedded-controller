#include "utest_rtc.h"
#include <string.h>

// Внутреннее состояние мока RTC
static struct {
    struct rtc_time datetime;
    struct rtc_alarm alarm;
    uint16_t offset;
    bool ready_read;
    bool alarm_flag_cleared;
    bool periodic_wakeup_disabled;

    // Track what was set
    bool datetime_was_set;
    struct rtc_time datetime_set_value;

    bool alarm_was_set;
    struct rtc_alarm alarm_set_value;

    bool offset_was_set;
    uint16_t offset_set_value;
} rtc_state;

void utest_rtc_reset(void)
{
    memset(&rtc_state, 0, sizeof(rtc_state));
}

void utest_rtc_set_ready_read(bool ready)
{
    rtc_state.ready_read = ready;
}

void utest_rtc_set_datetime(const struct rtc_time * tm)
{
    if (tm != NULL) {
        rtc_state.datetime = *tm;
    }
}

void utest_rtc_set_alarm(const struct rtc_alarm * alarm)
{
    if (alarm != NULL) {
        rtc_state.alarm = *alarm;
    }
}

void utest_rtc_set_offset(uint16_t offset)
{
    rtc_state.offset = offset;
}

void utest_rtc_set_alarm_flag(bool flag)
{
    rtc_state.alarm.flag = flag;
}

bool utest_rtc_get_was_datetime_set(struct rtc_time * tm)
{
    if (tm != NULL && rtc_state.datetime_was_set) {
        *tm = rtc_state.datetime_set_value;
    }
    return rtc_state.datetime_was_set;
}

bool utest_rtc_get_was_alarm_set(struct rtc_alarm * alarm)
{
    if (alarm != NULL && rtc_state.alarm_was_set) {
        *alarm = rtc_state.alarm_set_value;
    }
    return rtc_state.alarm_was_set;
}

bool utest_rtc_get_was_offset_set(uint16_t * offset)
{
    if (offset != NULL && rtc_state.offset_was_set) {
        *offset = rtc_state.offset_set_value;
    }
    return rtc_state.offset_was_set;
}

bool utest_rtc_was_alarm_flag_cleared(void)
{
    return rtc_state.alarm_flag_cleared;
}

bool utest_rtc_get_periodic_wakeup_disabled(void)
{
    return rtc_state.periodic_wakeup_disabled;
}

// Мок-реализация RTC API
void rtc_init(void)
{
    utest_rtc_reset();
}

void rtc_reset(void)
{
    utest_rtc_reset();
}

bool rtc_get_ready_read(void)
{
    return rtc_state.ready_read;
}

void rtc_get_datetime(struct rtc_time * tm)
{
    if (tm != NULL) {
        *tm = rtc_state.datetime;
    }
}

void rtc_set_datetime(const struct rtc_time * tm)
{
    if (tm != NULL) {
        rtc_state.datetime_was_set = true;
        rtc_state.datetime_set_value = *tm;
    }
}

void rtc_get_alarm(struct rtc_alarm * alarm)
{
    if (alarm != NULL) {
        *alarm = rtc_state.alarm;
    }
}

void rtc_set_alarm(const struct rtc_alarm * alarm)
{
    if (alarm != NULL) {
        rtc_state.alarm_was_set = true;
        rtc_state.alarm_set_value = *alarm;
    }
}

uint16_t rtc_get_offset(void)
{
    return rtc_state.offset;
}

void rtc_set_offset(uint16_t offset)
{
    rtc_state.offset_was_set = true;
    rtc_state.offset_set_value = offset;
}

void rtc_clear_alarm_flag(void)
{
    rtc_state.alarm_flag_cleared = true;
    rtc_state.alarm.flag = false;
}

void rtc_disable_periodic_wakeup(void)
{
    rtc_state.periodic_wakeup_disabled = true;
}

void rtc_enable_pc13_1hz_clkout(void) {}
void rtc_disable_pc13_1hz_clkout(void) {}
void rtc_enable_pa4_1hz_clkout(void) {}
void rtc_disable_pa4_1hz_clkout(void) {}
void rtc_set_periodic_wakeup(uint16_t period_s) { (void)period_s; }
void rtc_save_to_tamper_reg(uint8_t index, uint32_t data) { (void)index; (void)data; }
uint32_t rtc_get_tamper_reg(uint8_t index) { (void)index; return 0; }
