#include "utest_mcu_vbat.h"

static bool prepare_standby_called = false;

void mcu_vbat_init(void) {}

void mcu_vbat_check_do_periodic_work(void) {}

void mcu_vbat_prepare_standby(void)
{
    prepare_standby_called = true;
}

bool utest_mcu_vbat_was_prepare_standby_called(void)
{
    return prepare_standby_called;
}

void utest_mcu_vbat_reset(void)
{
    prepare_standby_called = false;
}
