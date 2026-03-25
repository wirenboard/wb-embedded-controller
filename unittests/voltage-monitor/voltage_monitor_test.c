#include "unity.h"
#include "voltage-monitor.h"
#include "adc.h"
#include "systick.h"
#include "utest_adc.h"
#include "utest_systick.h"

#define LOG_LEVEL LOG_LEVEL_INFO
#include "console_log.h"


void setUp(void)
{
    // Set initial time
    utest_systick_set_time_ms(1000);
}

void tearDown(void)
{

}

// Scenario: Initialize voltage monitor subsystem
// Expected: Not ready before init, not ready until start delay elapses,
// ready after delay
static void test_vmon_init(void)
{
    LOG_INFO("Testing initialization");

    TEST_ASSERT_FALSE_MESSAGE(vmon_ready(), "voltage-monitor shold not be ready before vmon_init() call");

    vmon_init();
    TEST_ASSERT_FALSE_MESSAGE(vmon_ready(), "voltage-monitor shold not be ready right after vmon_init() call");

    utest_systick_advance_time_ms(VOLTAGE_MONITOR_START_DELAY_MS);
    vmon_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(vmon_ready(), "voltage-monitor shold not be ready when VOLTAGE_MONITOR_START_DELAY_MS is not elapsed after vmon_init() call");

    utest_systick_advance_time_ms(1);
    vmon_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(vmon_ready(), "voltage-monitor should be ready when VOLTAGE_MONITOR_START_DELAY_MS elapsed after vmon_init() call");
}

// Scenario: Check voltage with values at normal, low, and high ranges
// Expected: Returns true for normal voltage, false for out-of-bounds voltages
static void test_vmon_check_voltage_bounds(void)
{
    LOG_INFO("Testing voltage bounds checker");

    // Normal voltage
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_5V, 5000);
    bool status = vmon_check_ch_once(VMON_CHANNEL_V50);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_check_ch_once() should return true when voltage is OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V50);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_get_ch_status() should return true when voltage is OK");

    // Low voltage
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_5V, 3000);
    status = vmon_check_ch_once(VMON_CHANNEL_V50);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_check_ch_once() should return false when voltage is below than FAIL min");
    status = vmon_get_ch_status(VMON_CHANNEL_V50);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_get_ch_status() should return false when voltage is below than FAIL min");

    // Normal voltage
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_5V, 5000);
    status = vmon_check_ch_once(VMON_CHANNEL_V50);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_check_ch_once() should return true when voltage is OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V50);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_get_ch_status() should return true when voltage is OK");

    // High voltage
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_5V, 6500);
    status = vmon_check_ch_once(VMON_CHANNEL_V50);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_check_ch_once() should return false when voltage is above than FAIL max");
    status = vmon_get_ch_status(VMON_CHANNEL_V50);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_get_ch_status() should return false when voltage is above than FAIL max");
}

// Scenario: Voltage rises from OK through hysteresis area to above FAIL max
// Expected: Status remains OK in hysteresis area (between OK max and FAIL max),
// changes to FAIL when above FAIL max
static void test_vmon_hysteresis_ok_to_fail_max(void)
{
    LOG_INFO("Testing hysteresis: OK -> FAIL max");

    // Normal voltage
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 3300);
    bool status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_check_ch_once() should return true when voltage is OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_get_ch_status() should return true when voltage is OK");

    // Voltage is between OK max and FAIL max
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 3450);
    status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "Voltage status should still be OK in the hysteresis area");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "Voltage status should still be OK in the hysteresis area");

    // Voltage is above FAIL max
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 3600);
    status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_check_ch_once() should return false when voltage is above than FAIL max");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_get_ch_status() should return false when voltage is above than FAIL max");
}

// Scenario: Voltage drops from OK through hysteresis area to below FAIL min
// Expected: Status remains OK in hysteresis area (between FAIL min and OK min),
// changes to FAIL when below FAIL min
static void test_vmon_hysteresis_ok_to_fail_min(void)
{
    LOG_INFO("Testing hysteresis: OK -> FAIL min");

    // Normal voltage
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 3300);
    bool status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_check_ch_once() should return true when voltage is OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_get_ch_status() should return true when voltage is OK");

    // Voltage is between FAIL min and OK min (hysteresis area)
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 2850);
    status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "Voltage status should still be OK in the hysteresis area");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "Voltage status should still be OK in the hysteresis area");

    // Voltage is below FAIL min
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 2500);
    status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_check_ch_once() should return false when voltage is below than FAIL min");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_get_ch_status() should return false when voltage is below than FAIL min");
}

// Scenario: Voltage rises from below FAIL min through hysteresis area to OK
// Expected: Status remains FAIL in hysteresis area, changes to OK when above
// OK min
static void test_vmon_hysteresis_fail_min_to_ok(void)
{
    LOG_INFO("Testing hysteresis: FAIL min -> OK");

    // Voltage is below FAIL min
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 2500);
    bool status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_check_ch_once() should return false when voltage is below FAIL min");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_get_ch_status() should return false when voltage is below FAIL min");

    // Voltage is between FAIL min and OK min (hysteresis area)
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 2850);
    status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "Voltage status should still be FAIL in the hysteresis area");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "Voltage status should still be FAIL in the hysteresis area");

    // Normal voltage
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 3300);
    status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_check_ch_once() should return true when voltage is OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_get_ch_status() should return true when voltage is OK");
}

// Scenario: Voltage drops from above FAIL max through hysteresis area to OK
// Expected: Status remains FAIL in hysteresis area, changes to OK when below
// OK max
static void test_vmon_hysteresis_fail_max_to_ok(void)
{
    LOG_INFO("Testing hysteresis: FAIL max -> OK");

    // Voltage is above FAIL max
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 3600);
    bool status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_check_ch_once() should return false when voltage is above FAIL max");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_get_ch_status() should return false when voltage is above FAIL max");

    // Voltage is between OK max and FAIL max (hysteresis area)
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 3450);
    status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "Voltage status should still be FAIL in the hysteresis area");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "Voltage status should still be FAIL in the hysteresis area");

    // Normal voltage
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 3300);
    status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_check_ch_once() should return true when voltage is OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_get_ch_status() should return true when voltage is OK");
}

// Scenario: Run periodic work after start delay with all voltages correct
// Expected: Module becomes ready, all channels report OK status
static void test_vmon_do_periodic_work_after_delay(void)
{
    LOG_INFO("Testing periodic work after start delay");

    vmon_init();

    // Set correct voltages
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_V_IN, 20000);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 3300);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_5V, 5000);

    // Advance time past the delay
    utest_systick_advance_time_ms(VOLTAGE_MONITOR_START_DELAY_MS + 10);
    vmon_do_periodic_work();

    // Module should be ready
    TEST_ASSERT_TRUE_MESSAGE(vmon_ready(), "Module should be ready after start delay");

    // All channels should have OK status
    bool status = vmon_get_ch_status(VMON_CHANNEL_V_IN);
    TEST_ASSERT_TRUE_MESSAGE(status, "V_IN should be OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "V33 should be OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V50);
    TEST_ASSERT_TRUE_MESSAGE(status, "V50 should be OK");
}

// Scenario: Run periodic work with one channel out of limits
// Expected: Failed channel reports FAIL, other channels report independent
// status (OK)
static void test_vmon_do_periodic_work_checks_all_channels(void)
{
    LOG_INFO("Testing periodic work checks all channels");

    vmon_init();

    // Set one channel out of limits
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_V_IN, 20000);  // OK
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 2500);    // FAIL (below OK min)
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_5V, 5000);     // OK

    // Advance time and run periodic work
    utest_systick_advance_time_ms(VOLTAGE_MONITOR_START_DELAY_MS + 10);
    vmon_do_periodic_work();

    // Check status of all channels
    bool status = vmon_get_ch_status(VMON_CHANNEL_V_IN);
    TEST_ASSERT_TRUE_MESSAGE(status, "V_IN should be OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "V33 should be FAIL");
    status = vmon_get_ch_status(VMON_CHANNEL_V50);
    TEST_ASSERT_TRUE_MESSAGE(status, "V50 should be OK");
}

// Scenario: Set different voltage levels on different channels
// Expected: Each channel reports independent status (OK/FAIL) based on its
// voltage level
static void test_vmon_multiple_channels_independent(void)
{
    LOG_INFO("Testing multiple channels independence");

    // Set different voltages for different channels
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_V_IN, 20000);  // OK
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 3300);    // OK
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_5V, 3000);     // FAIL (below OK min)

    // Check channels
    bool status = vmon_check_ch_once(VMON_CHANNEL_V_IN);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_check_ch_once() should return true for V_IN");
    status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_check_ch_once() should return true for V33");
    status = vmon_check_ch_once(VMON_CHANNEL_V50);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_check_ch_once() should return false for V50");

    // Verify status independence
    status = vmon_get_ch_status(VMON_CHANNEL_V_IN);
    TEST_ASSERT_TRUE_MESSAGE(status, "V_IN status should be OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "V33 status should be OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V50);
    TEST_ASSERT_FALSE_MESSAGE(status, "V50 status should be FAIL");
}


int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_vmon_init);
    RUN_TEST(test_vmon_check_voltage_bounds);
    RUN_TEST(test_vmon_hysteresis_ok_to_fail_max);
    RUN_TEST(test_vmon_hysteresis_ok_to_fail_min);
    RUN_TEST(test_vmon_hysteresis_fail_min_to_ok);
    RUN_TEST(test_vmon_hysteresis_fail_max_to_ok);
    RUN_TEST(test_vmon_do_periodic_work_after_delay);
    RUN_TEST(test_vmon_do_periodic_work_checks_all_channels);
    RUN_TEST(test_vmon_multiple_channels_independent);

    return UNITY_END();
}
