#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>
#include "irq-subsystem.h"

// ======================== linux-power-control ========================
void utest_linux_pwr_reset(void);
void utest_linux_pwr_set_is_busy(bool busy);
void utest_linux_pwr_set_standby_exit_jmp(jmp_buf *jmp);
bool utest_linux_pwr_get_init_called(void);
bool utest_linux_pwr_get_init_on(void);
bool utest_linux_pwr_get_pwr_on_called(void);
bool utest_linux_pwr_get_hard_off_called(void);
bool utest_linux_pwr_get_hard_reset_called(void);
bool utest_linux_pwr_get_reset_pmic_called(void);
bool utest_linux_pwr_get_standby_called(void);
uint16_t utest_linux_pwr_get_standby_wakeup_s(void);

// ======================== pwrkey ========================
void utest_pwrkey_reset(void);
void utest_pwrkey_set_ready(bool ready);
void utest_pwrkey_set_pressed(bool pressed);
void utest_pwrkey_set_short_press(bool val);
void utest_pwrkey_set_long_press(bool val);

// ======================== wdt ========================
void utest_wdt_reset(void);
void utest_wdt_set_timed_out(bool val);
bool utest_wdt_get_started(void);
uint16_t utest_wdt_get_timeout(void);

// ======================== temperature-control ========================
void utest_temp_ctrl_reset(void);
void utest_temp_ctrl_set_ready(bool ready);
void utest_temp_ctrl_set_temperature_c_x100(int16_t temp);

// ======================== rtc ========================
void utest_rtc_reset(void);
bool utest_rtc_get_periodic_wakeup_disabled(void);

// ======================== rtc-alarm-subsystem ========================
void utest_rtc_alarm_reset(void);
void utest_rtc_alarm_set_enabled(bool enabled);

// ======================== irq-subsystem ========================
void utest_irq_reset(void);
irq_flags_t utest_irq_get_set_flags(void);
