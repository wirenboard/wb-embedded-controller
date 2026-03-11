#include "unity.h"

#include "linux-power-control.h"
#include "utest_mcu_pwr.h"
#include "utest_wbmcu_system.h"

void setUp(void)
{
    utest_mcu_reset();
    utest_pwr_reset();
}

void tearDown(void)
{

}

void test_linux_cpu_pwr_seq_off_and_goto_standby(void)
{
    const uint16_t wakeup_after_s = 123;
    const uint32_t linux_power_pin_mask = (1U << 1);

    linux_cpu_pwr_seq_off_and_goto_standby(wakeup_after_s);

    TEST_ASSERT_BITS_HIGH_MESSAGE(linux_power_pin_mask, PWR->PDCRD,
                                  "Pull-down must be enabled for linux power pin before standby");
    TEST_ASSERT_BITS_HIGH_MESSAGE(PWR_CR3_APC, PWR->CR3,
                                  "APC bit must be set to apply pull-up/pull-down configuration");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(wakeup_after_s, utest_mcu_get_standby_wakeup_time(),
                                     "Standby wakeup timeout must be passed to mcu_goto_standby");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_linux_cpu_pwr_seq_off_and_goto_standby);

    return UNITY_END();
}
