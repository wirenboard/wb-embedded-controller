#include <stdbool.h>
#include <stdint.h>

static bool pwrkey_long_press = false;
static bool pwrkey_is_pressed = false;
static bool wbmz_stepup_enabled = false;
static uint32_t pwrkey_periodic_work_call_count = 0;
static uint32_t wbmz_periodic_work_call_count = 0;
static uint32_t wbmz_disable_stepup_call_count = 0;

void utest_linux_power_control_stubs_reset(void)
{
    pwrkey_long_press = false;
    pwrkey_is_pressed = false;
    wbmz_stepup_enabled = false;
    pwrkey_periodic_work_call_count = 0;
    wbmz_periodic_work_call_count = 0;
    wbmz_disable_stepup_call_count = 0;
}

void utest_linux_power_control_set_pwrkey_long_press(bool value)
{
    pwrkey_long_press = value;
}

void utest_linux_power_control_set_pwrkey_pressed(bool value)
{
    pwrkey_is_pressed = value;
}

void utest_linux_power_control_set_wbmz_stepup_enabled(bool value)
{
    wbmz_stepup_enabled = value;
}

uint32_t utest_linux_power_control_get_pwrkey_periodic_work_call_count(void)
{
    return pwrkey_periodic_work_call_count;
}

uint32_t utest_linux_power_control_get_wbmz_periodic_work_call_count(void)
{
    return wbmz_periodic_work_call_count;
}

uint32_t utest_linux_power_control_get_wbmz_disable_stepup_call_count(void)
{
    return wbmz_disable_stepup_call_count;
}

void console_print(const char str[])
{
    (void)str;
}

void console_print_w_prefix(const char str[])
{
    (void)str;
}

bool pwrkey_handle_long_press(void)
{
    return pwrkey_long_press;
}

bool pwrkey_pressed(void)
{
    return pwrkey_is_pressed;
}

void pwrkey_do_periodic_work(void)
{
    pwrkey_periodic_work_call_count++;
}

void wbmz_disable_stepup(void)
{
    wbmz_stepup_enabled = false;
    wbmz_disable_stepup_call_count++;
}

bool wbmz_is_stepup_enabled(void)
{
    return wbmz_stepup_enabled;
}

void wbmz_do_periodic_work(void)
{
    wbmz_periodic_work_call_count++;
}
