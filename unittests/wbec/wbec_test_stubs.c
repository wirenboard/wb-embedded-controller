#include "wbec_test_stubs.h"
#include "rtc.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <setjmp.h>

// ======================== linux-power-control ========================

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

void utest_linux_pwr_set_is_busy(bool busy)
{
    linux_pwr_state.is_busy = busy;
}

void utest_linux_pwr_set_standby_exit_jmp(jmp_buf *jmp)
{
    linux_pwr_state.standby_exit_jmp = jmp;
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

bool utest_linux_pwr_get_standby_called(void)
{
    return linux_pwr_state.standby_called;
}

uint16_t utest_linux_pwr_get_standby_wakeup_s(void)
{
    return linux_pwr_state.standby_wakeup_s;
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

// ======================== pwrkey ========================

static struct {
    bool ready;
    bool pressed;
    bool short_press;
    bool long_press;
} pwrkey_state = {
    .ready = true,
};

void utest_pwrkey_reset(void)
{
    pwrkey_state.ready = true;
    pwrkey_state.pressed = false;
    pwrkey_state.short_press = false;
    pwrkey_state.long_press = false;
}

void utest_pwrkey_set_ready(bool ready)
{
    pwrkey_state.ready = ready;
}

void utest_pwrkey_set_pressed(bool pressed)
{
    pwrkey_state.pressed = pressed;
}

void utest_pwrkey_set_short_press(bool val)
{
    pwrkey_state.short_press = val;
}

void utest_pwrkey_set_long_press(bool val)
{
    pwrkey_state.long_press = val;
}

void pwrkey_init(void) {}

bool pwrkey_ready(void)
{
    return pwrkey_state.ready;
}

bool pwrkey_pressed(void)
{
    return pwrkey_state.pressed;
}

void pwrkey_do_periodic_work(void) {}

bool pwrkey_handle_short_press(void)
{
    bool ret = pwrkey_state.short_press;
    pwrkey_state.short_press = false;
    return ret;
}

bool pwrkey_handle_long_press(void)
{
    bool ret = pwrkey_state.long_press;
    pwrkey_state.long_press = false;
    return ret;
}

// ======================== wdt ========================

static struct {
    uint16_t timeout;
    bool started;
    bool timed_out;
} wdt_state = {0};

void utest_wdt_reset(void)
{
    memset(&wdt_state, 0, sizeof(wdt_state));
}

void utest_wdt_set_timed_out(bool val)
{
    wdt_state.timed_out = val;
}

bool utest_wdt_get_started(void)
{
    return wdt_state.started;
}

uint16_t utest_wdt_get_timeout(void)
{
    return wdt_state.timeout;
}

void wdt_set_timeout(uint16_t secs)
{
    wdt_state.timeout = secs;
}

void wdt_start_reset(void)
{
    wdt_state.started = true;
}

void wdt_stop(void)
{
    wdt_state.started = false;
}

bool wdt_handle_timed_out(void)
{
    bool ret = wdt_state.timed_out;
    wdt_state.timed_out = false;
    return ret;
}

void wdt_do_periodic_work(void) {}

// ======================== temperature-control ========================

static struct {
    bool temperature_ready;
    int16_t temperature_c_x100;
} temp_ctrl_state = {
    .temperature_ready = true,
};

void utest_temp_ctrl_reset(void)
{
    temp_ctrl_state.temperature_ready = true;
    temp_ctrl_state.temperature_c_x100 = 2500;
}

void utest_temp_ctrl_set_ready(bool ready)
{
    temp_ctrl_state.temperature_ready = ready;
}

void utest_temp_ctrl_set_temperature_c_x100(int16_t temp)
{
    temp_ctrl_state.temperature_c_x100 = temp;
}

void temperature_control_init(void) {}
void temperature_control_do_periodic_work(void) {}

bool temperature_control_is_temperature_ready(void)
{
    return temp_ctrl_state.temperature_ready;
}

int16_t temperature_control_get_temperature_c_x100(void)
{
    return temp_ctrl_state.temperature_c_x100;
}

void temperature_control_heater_force_control(bool force_enable)
{
    (void)force_enable;
}

// ======================== rtc ========================

static bool periodic_wakeup_disabled = false;

void utest_rtc_reset(void)
{
    periodic_wakeup_disabled = false;
}

bool utest_rtc_get_periodic_wakeup_disabled(void)
{
    return periodic_wakeup_disabled;
}

void rtc_init(void) {}
void rtc_reset(void) {}
bool rtc_get_ready_read(void) { return true; }
void rtc_get_datetime(struct rtc_time * tm) { memset(tm, 0, sizeof(*tm)); }
void rtc_set_datetime(const struct rtc_time * tm) { (void)tm; }
void rtc_get_alarm(struct rtc_alarm * alarm) { memset(alarm, 0, sizeof(*alarm)); }
void rtc_set_alarm(const struct rtc_alarm * alarm) { (void)alarm; }
uint16_t rtc_get_offset(void) { return 0; }
void rtc_set_offset(uint16_t offset) { (void)offset; }
void rtc_clear_alarm_flag(void) {}
void rtc_enable_pc13_1hz_clkout(void) {}
void rtc_disable_pc13_1hz_clkout(void) {}
void rtc_enable_pa4_1hz_clkout(void) {}
void rtc_disable_pa4_1hz_clkout(void) {}
void rtc_set_periodic_wakeup(uint16_t period_s) { (void)period_s; }

void rtc_disable_periodic_wakeup(void)
{
    periodic_wakeup_disabled = true;
}

void rtc_save_to_tamper_reg(uint8_t index, uint32_t data) { (void)index; (void)data; }
uint32_t rtc_get_tamper_reg(uint8_t index) { (void)index; return 0; }

// ======================== rtc-alarm-subsystem ========================

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

// ======================== irq-subsystem ========================

static irq_flags_t irq_flags_set = 0;

void utest_irq_reset(void)
{
    irq_flags_set = 0;
}

irq_flags_t utest_irq_get_set_flags(void)
{
    return irq_flags_set;
}

void irq_set_flag(enum irq_flag f)
{
    irq_flags_set |= (1u << f);
}

irq_flags_t irq_get_flags(void) { return 0; }
void irq_set_mask(irq_flags_t m) { (void)m; }
void irq_clear_flags(irq_flags_t f) { (void)f; }
void irq_init(void) {}
void irq_do_periodic_work(void) {}

// ======================== usart_tx ========================

void usart_tx_init(void) {}
void usart_tx_deinit(void) {}
void usart_tx_buf_blocking(const void * buf, size_t size) { (void)buf; (void)size; }
void usart_tx_str_blocking(const char str[]) { (void)str; }

// ======================== buzzer ========================

void buzzer_init(void) {}
void buzzer_beep(uint16_t freq, uint16_t duration_ms) { (void)freq; (void)duration_ms; }
void buzzer_subsystem_do_periodic_work(void) {}

// ======================== hwrev ========================

#include "hwrev.h"

void hwrev_init(void) {}
enum hwrev hwrev_get(void) { return WBEC_HWREV; }
bool hwrev_is_valid(void) { return true; }

// ======================== rcc ========================

void rcc_set_hsi_pll_64mhz_clock(void) {}
