#include "wbec_test_stubs.h"

#include <stddef.h>
#include <string.h>

static struct {
    bool init_called;
    bool init_on;
    bool pwr_on_called;
    bool hard_off_called;
    bool hard_reset_called;
    bool reset_pmic_called;
    bool is_busy;
    bool standby_called;
    uint16_t standby_wakeup_s;
    jmp_buf *standby_exit_jmp;
} linux_pwr_state = {0};

void utest_linux_pwr_reset(void)
{
    memset(&linux_pwr_state, 0, sizeof(linux_pwr_state));
}

void utest_linux_pwr_set_standby_exit_jmp(jmp_buf *jmp)
{
    linux_pwr_state.standby_exit_jmp = jmp;
}

bool utest_linux_pwr_get_standby_called(void)
{
    return linux_pwr_state.standby_called;
}

uint16_t utest_linux_pwr_get_standby_wakeup_s(void)
{
    return linux_pwr_state.standby_wakeup_s;
}

void utest_linux_pwr_set_busy(bool busy)
{
    linux_pwr_state.is_busy = busy;
}

bool utest_linux_pwr_get_init_called(void)
{
    return linux_pwr_state.init_called;
}

bool utest_linux_pwr_get_init_on(void)
{
    return linux_pwr_state.init_on;
}

bool utest_linux_pwr_get_pwr_on_called(void)
{
    return linux_pwr_state.pwr_on_called;
}

bool utest_linux_pwr_get_hard_off_called(void)
{
    return linux_pwr_state.hard_off_called;
}

bool utest_linux_pwr_get_hard_reset_called(void)
{
    return linux_pwr_state.hard_reset_called;
}

bool utest_linux_pwr_get_reset_pmic_called(void)
{
    return linux_pwr_state.reset_pmic_called;
}

void linux_cpu_pwr_seq_init(bool on)
{
    linux_pwr_state.init_called = true;
    linux_pwr_state.init_on = on;
}

void linux_cpu_pwr_seq_off_and_goto_standby(uint16_t wakeup_after_s)
{
    linux_pwr_state.standby_called = true;
    linux_pwr_state.standby_wakeup_s = wakeup_after_s;
    if (linux_pwr_state.standby_exit_jmp) {
        longjmp(*linux_pwr_state.standby_exit_jmp, 1);
    }
}

void linux_cpu_pwr_seq_on(void)
{
    linux_pwr_state.pwr_on_called = true;
}

void linux_cpu_pwr_seq_hard_off(void)
{
    linux_pwr_state.hard_off_called = true;
}

void linux_cpu_pwr_seq_hard_reset(void)
{
    linux_pwr_state.hard_reset_called = true;
}

void linux_cpu_pwr_seq_reset_pmic(void)
{
    linux_pwr_state.reset_pmic_called = true;
}

bool linux_cpu_pwr_seq_is_busy(void)
{
    return linux_pwr_state.is_busy;
}

void linux_cpu_pwr_seq_do_periodic_work(void) {}

static struct {
    bool temperature_ready;
    int16_t temperature_c_x100;
} temp_ctrl_state = {
    .temperature_ready = true,
};

void utest_temp_set_ready(bool ready)
{
    temp_ctrl_state.temperature_ready = ready;
}

bool temperature_control_is_temperature_ready(void)
{
    return temp_ctrl_state.temperature_ready;
}

int16_t temperature_control_get_temperature_c_x100(void)
{
    return temp_ctrl_state.temperature_c_x100;
}

static bool alarm_enabled = false;

void utest_rtc_alarm_reset(void)
{
    alarm_enabled = false;
}

void utest_rtc_alarm_set_enabled(bool enabled)
{
    alarm_enabled = enabled;
}

bool rtc_alarm_is_alarm_enabled(void)
{
    return alarm_enabled;
}

void rtc_alarm_do_periodic_work(void) {}

void usart_tx_buf_blocking(const void * buf, size_t size) { (void)buf; (void)size; }
void buzzer_beep(uint16_t freq, uint16_t duration_ms) { (void)freq; (void)duration_ms; }
void rcc_set_hsi_pll_64mhz_clock(void) {}
