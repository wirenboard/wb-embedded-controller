#include "utest_pwrkey.h"

static bool pwrkey_long_press = false;
static bool pwrkey_short_press = false;
static bool pwrkey_is_pressed = false;
static bool pwrkey_is_ready = true;
static uint32_t pwrkey_periodic_work_call_count = 0;

void utest_pwrkey_reset(void)
{
    pwrkey_long_press = false;
    pwrkey_short_press = false;
    pwrkey_is_pressed = false;
    pwrkey_is_ready = true;
    pwrkey_periodic_work_call_count = 0;
}

void utest_set_pwrkey_long_press(bool value)
{
    pwrkey_long_press = value;
}

void utest_set_pwrkey_pressed(bool value)
{
    pwrkey_is_pressed = value;
}

void utest_pwrkey_set_ready(bool ready)
{
    pwrkey_is_ready = ready;
}

void utest_pwrkey_set_short_press(bool value)
{
    pwrkey_short_press = value;
}

uint32_t utest_pwrkey_get_periodic_work_call_count(void)
{
    return pwrkey_periodic_work_call_count;
}

bool pwrkey_handle_long_press(void)
{
    bool ret = pwrkey_long_press;
    pwrkey_long_press = false;
    return ret;
}

bool pwrkey_handle_short_press(void)
{
    bool ret = pwrkey_short_press;
    pwrkey_short_press = false;
    return ret;
}

bool pwrkey_ready(void)
{
    return pwrkey_is_ready;
}

bool pwrkey_pressed(void)
{
    return pwrkey_is_pressed;
}

void pwrkey_do_periodic_work(void)
{
    pwrkey_periodic_work_call_count++;
}
