#pragma once

bool rtc_alarm_is_alarm_enabled(void);
bool rtc_alarm_take_fired(void);
void rtc_alarm_do_periodic_work(void);
