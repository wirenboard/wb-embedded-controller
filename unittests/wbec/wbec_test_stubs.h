#pragma once

#include "irq-subsystem.h"

#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>

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
void utest_temp_set_ready(bool ready);
void utest_rtc_alarm_reset(void);
void utest_rtc_alarm_set_enabled(bool enabled);
