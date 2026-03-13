#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>
#include "irq-subsystem.h"

// ======================== linux-power-control ========================
void utest_linux_pwr_reset(void);
void utest_linux_pwr_set_standby_exit_jmp(jmp_buf *jmp);
void utest_linux_pwr_set_busy(bool busy);
bool utest_linux_pwr_get_standby_called(void);
uint16_t utest_linux_pwr_get_standby_wakeup_s(void);
bool utest_linux_pwr_get_init_called(void);
bool utest_linux_pwr_get_init_on(void);
bool utest_linux_pwr_get_pwr_on_called(void);
bool utest_linux_pwr_get_hard_off_called(void);
bool utest_linux_pwr_get_hard_reset_called(void);
bool utest_linux_pwr_get_reset_pmic_called(void);

// ======================== pwrkey ========================
void utest_pwrkey_reset(void);
void utest_pwrkey_set_ready(bool ready);
void utest_pwrkey_set_pressed(bool pressed);
void utest_pwrkey_set_short_press(bool value);
void utest_pwrkey_set_long_press(bool value);

// ======================== wdt ========================
void utest_wdt_reset(void);
void utest_wdt_set_timed_out(bool value);
uint16_t utest_wdt_get_timeout(void);
bool utest_wdt_get_started(void);

// ======================== rtc ========================
void utest_rtc_reset(void);
bool utest_rtc_get_periodic_wakeup_disabled(void);

// ======================== rtc-alarm-subsystem ========================
void utest_rtc_alarm_reset(void);
void utest_rtc_alarm_set_enabled(bool enabled);

// ======================== temperature-control ========================
void utest_temp_set_ready(bool ready);

// ======================== irq-subsystem ========================
void utest_irq_reset(void);
irq_flags_t utest_irq_get_set_flags(void);
