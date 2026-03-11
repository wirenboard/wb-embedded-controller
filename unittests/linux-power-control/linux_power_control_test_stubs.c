#include <stdbool.h>

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
    return false;
}

bool pwrkey_pressed(void)
{
    return false;
}

void pwrkey_do_periodic_work(void)
{

}

void wbmz_disable_stepup(void)
{

}

bool wbmz_is_stepup_enabled(void)
{
    return false;
}

void wbmz_do_periodic_work(void)
{

}
