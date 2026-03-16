#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "rtc.h"

// Mock functions for testing RTC
void utest_rtc_reset(void);
void utest_rtc_set_ready_read(bool ready);
void utest_rtc_set_datetime(const struct rtc_time * tm);
void utest_rtc_set_alarm(const struct rtc_alarm * alarm);
void utest_rtc_set_offset(uint16_t offset);
void utest_rtc_set_alarm_flag(bool flag);

// Get functions to verify what was written
bool utest_rtc_get_was_datetime_set(struct rtc_time * tm);
bool utest_rtc_get_was_alarm_set(struct rtc_alarm * alarm);
bool utest_rtc_get_was_offset_set(uint16_t * offset);
bool utest_rtc_was_alarm_flag_cleared(void);
bool utest_rtc_get_periodic_wakeup_disabled(void);

// Вспомогательные функции для мока rtc-alarm-subsystem
void utest_rtc_alarm_reset(void);
void utest_rtc_alarm_set_enabled(bool enabled);
