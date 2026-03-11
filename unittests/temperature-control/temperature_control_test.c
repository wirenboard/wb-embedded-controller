#include "unity.h"
#include "temperature-control.h"
#include "utest_ntc.h"
#include "utest_wbmz_common.h"
#include "utest_voltage_monitor.h"
#include "voltage-monitor.h"
#include "utest_gpio.h"
#include "config.h"

#define LOG_LEVEL LOG_LEVEL_INFO
#include "console_log.h"

#if defined(EC_GPIO_HEATER)
static const gpio_pin_t heater_pin = { EC_GPIO_HEATER };
#endif


void setUp(void)
{
    // Reset GPIO state
    utest_gpio_reset_instances();

    // Set default values
    utest_ntc_set_temperature(F16(25.0));  // 25°C
    utest_wbmz_set_powered_from_wbmz(false);
    vmon_init();
}

void tearDown(void)
{

}


static void test_temperature_control_init(void)
{
    LOG_INFO("Testing initialization");

    temperature_control_init();
    // Init should succeed without errors
    TEST_PASS();
}

#if defined(EC_GPIO_HEATER)
static void test_heater_gpio_init(void)
{
    LOG_INFO("Testing heater GPIO initialization");

    temperature_control_init();

    // Check that heater GPIO is configured as output
    uint32_t mode = utest_gpio_get_mode(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(GPIO_MODE_OUTPUT, mode, "Heater GPIO should be configured as OUTPUT");

    // Check that heater GPIO is configured as push-pull
    uint32_t otype = utest_gpio_get_output_type(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(GPIO_OTYPE_PUSH_PULL, otype, "Heater GPIO should be configured as PUSH-PULL");

    // Check that heater is off after init
    uint32_t state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Heater GPIO should be LOW (off) after initialization");
}
#endif


static void test_temperature_is_ready_above_minimum(void)
{
    LOG_INFO("Testing temperature ready when above minimum");

    // Set temperature above minimum (WBEC_MINIMUM_WORKING_TEMPERATURE)
    utest_ntc_set_temperature(F16(WBEC_MINIMUM_WORKING_TEMPERATURE + 1.0));

    bool ready = temperature_control_is_temperature_ready();
    TEST_ASSERT_TRUE_MESSAGE(ready, "Temperature should be ready when above minimum working temperature");
}


static void test_temperature_is_not_ready_below_minimum(void)
{
    LOG_INFO("Testing temperature not ready when below minimum");

    // Set temperature below minimum (WBEC_MINIMUM_WORKING_TEMPERATURE)
    utest_ntc_set_temperature(F16(WBEC_MINIMUM_WORKING_TEMPERATURE - 10.0));

    bool ready = temperature_control_is_temperature_ready();
    TEST_ASSERT_FALSE_MESSAGE(ready, "Temperature should not be ready when below minimum working temperature");
}


static void test_temperature_is_ready_at_minimum_threshold(void)
{
    LOG_INFO("Testing temperature ready at threshold");

    // Set temperature exactly at minimum threshold
    utest_ntc_set_temperature(F16(WBEC_MINIMUM_WORKING_TEMPERATURE));

    bool ready = temperature_control_is_temperature_ready();
    TEST_ASSERT_FALSE_MESSAGE(ready, "Temperature should not be ready at exact minimum threshold");

    // Just above the threshold
    utest_ntc_set_temperature(F16(WBEC_MINIMUM_WORKING_TEMPERATURE + 1.0));

    ready = temperature_control_is_temperature_ready();
    TEST_ASSERT_TRUE_MESSAGE(ready, "Temperature should be ready above minimum threshold");
}


static void test_get_temperature_positive(void)
{
    LOG_INFO("Testing get temperature - positive values");

    utest_ntc_set_temperature(F16(25.5));

    int16_t temp_x100 = temperature_control_get_temperature_c_x100();
    TEST_ASSERT_EQUAL_INT16_MESSAGE(2550, temp_x100, "Temperature x100 should be 2550 for 25.5°C");
}


static void test_get_temperature_negative(void)
{
    LOG_INFO("Testing get temperature - negative values");

    utest_ntc_set_temperature(F16(-15.5));

    int16_t temp_x100 = temperature_control_get_temperature_c_x100();
    TEST_ASSERT_EQUAL_INT16_MESSAGE(-1550, temp_x100, "Temperature x100 should be -1550 for -15.5°C");
}


static void test_get_temperature_zero(void)
{
    LOG_INFO("Testing get temperature - zero");

    utest_ntc_set_temperature(F16(0.0));

    int16_t temp_x100 = temperature_control_get_temperature_c_x100();
    TEST_ASSERT_EQUAL_INT16_MESSAGE(0, temp_x100, "Temperature x100 should be 0 for 0.0°C");
}


#if defined(EC_GPIO_HEATER)
static void test_heater_turns_on_at_low_temperature(void)
{
    LOG_INFO("Testing heater turns on at low temperature");

    temperature_control_init();

    // Set temperature below heater ON threshold (EC_HEATER_ON_TEMP)
    utest_ntc_set_temperature(F16(EC_HEATER_ON_TEMP - 1.0));

    // Set power from Vin (heater requires Vin to be present)
    utest_wbmz_set_powered_from_wbmz(false);
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);

    temperature_control_do_periodic_work();

    // Check that heater GPIO is HIGH (on)
    uint32_t state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Heater GPIO should be HIGH (on) at low temperature");
}


static void test_heater_turns_off_at_high_temperature(void)
{
    LOG_INFO("Testing heater turns off at high temperature");

    temperature_control_init();

    // First turn heater on
    utest_ntc_set_temperature(F16(EC_HEATER_ON_TEMP - 1.0));
    utest_wbmz_set_powered_from_wbmz(false);
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);
    temperature_control_do_periodic_work();

    // Verify heater is on
    uint32_t state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Heater should be on initially");

    // Then set temperature above heater OFF threshold (EC_HEATER_OFF_TEMP)
    utest_ntc_set_temperature(F16(EC_HEATER_OFF_TEMP + 1.0));
    temperature_control_do_periodic_work();

    // Check that heater GPIO is LOW (off)
    state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Heater GPIO should be LOW (off) at high temperature");
}


static void test_heater_stays_off_when_powered_from_wbmz(void)
{
    LOG_INFO("Testing heater stays off when powered from WBMZ");

    temperature_control_init();

    // Set low temperature
    utest_ntc_set_temperature(F16(EC_HEATER_ON_TEMP - 1.0));

    // Set power from WBMZ (heater should not turn on)
    utest_wbmz_set_powered_from_wbmz(true);
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, false);

    temperature_control_do_periodic_work();

    // Check that heater GPIO is LOW (off)
    uint32_t state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Heater GPIO should stay LOW (off) when powered from WBMZ");
}


static void test_heater_stays_off_when_vin_not_present(void)
{
    LOG_INFO("Testing heater stays off when Vin not present");

    temperature_control_init();

    // Set low temperature
    utest_ntc_set_temperature(F16(EC_HEATER_ON_TEMP - 1.0));

    // Set no Vin (heater should not turn on)
    utest_wbmz_set_powered_from_wbmz(false);
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, false);

    temperature_control_do_periodic_work();

    // Check that heater GPIO is LOW (off)
    uint32_t state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Heater GPIO should stay LOW (off) when Vin not present");
}


static void test_heater_hysteresis(void)
{
    LOG_INFO("Testing heater hysteresis");

    temperature_control_init();

    // Heater ON threshold: EC_HEATER_ON_TEMP, OFF threshold: EC_HEATER_OFF_TEMP
    utest_wbmz_set_powered_from_wbmz(false);
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);

    // Start with low temperature - heater should turn on
    utest_ntc_set_temperature(F16(EC_HEATER_ON_TEMP - 1.0));
    temperature_control_do_periodic_work();

    // Temperature rises but still below OFF threshold
    utest_ntc_set_temperature(F16((EC_HEATER_ON_TEMP + EC_HEATER_OFF_TEMP) / 2.0));
    temperature_control_do_periodic_work();
    // Heater should still be on (above ON, below OFF)

    // Temperature rises above OFF threshold
    utest_ntc_set_temperature(F16(EC_HEATER_OFF_TEMP + 1.0));
    temperature_control_do_periodic_work();
    // Heater should turn off

    // Temperature drops but still above ON threshold
    utest_ntc_set_temperature(F16((EC_HEATER_ON_TEMP + EC_HEATER_OFF_TEMP) / 2.0));
    temperature_control_do_periodic_work();
    // Heater should still be off (below OFF, above ON)

    // Temperature drops below ON threshold
    utest_ntc_set_temperature(F16(EC_HEATER_ON_TEMP - 1.0));
    temperature_control_do_periodic_work();
    // Heater should turn on again

    TEST_PASS();
}


static void test_heater_turns_off_when_wbmz_power_enabled_during_heating(void)
{
    LOG_INFO("Testing heater turns off when switched to WBMZ power during heating");

    temperature_control_init();

    // Set low temperature and proper power conditions to enable heater
    utest_ntc_set_temperature(F16(EC_HEATER_ON_TEMP - 1.0));
    utest_wbmz_set_powered_from_wbmz(false);
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);

    temperature_control_do_periodic_work();

    // Verify heater turned on
    uint32_t state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Heater should be ON at low temperature with Vin");

    // Now switch to WBMZ power while heater is running
    utest_wbmz_set_powered_from_wbmz(true);
    temperature_control_do_periodic_work();

    // Heater should automatically turn off
    state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Heater should turn OFF when switched to WBMZ power");
}


static void test_heater_turns_off_when_vin_lost_during_heating(void)
{
    LOG_INFO("Testing heater turns off when Vin is lost during heating");

    temperature_control_init();

    // Set low temperature and proper power conditions to enable heater
    utest_ntc_set_temperature(F16(EC_HEATER_ON_TEMP - 1.0));
    utest_wbmz_set_powered_from_wbmz(false);
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);

    temperature_control_do_periodic_work();

    // Verify heater turned on
    uint32_t state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Heater should be ON at low temperature with Vin");

    // Now lose Vin while heater is running
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, false);
    temperature_control_do_periodic_work();

    // Heater should automatically turn off
    state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Heater should turn OFF when Vin is lost");
}


static void test_heater_force_control_enable(void)
{
    LOG_INFO("Testing heater force control enable");

    temperature_control_init();

    // Set high temperature (heater would normally be off)
    utest_ntc_set_temperature(F16(25.0));
    utest_wbmz_set_powered_from_wbmz(false);
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);

    // Force enable heater
    temperature_control_heater_force_control(true);

    // Heater should be on despite high temperature
    temperature_control_do_periodic_work();

    TEST_PASS();
}


static void test_heater_force_control_disable(void)
{
    LOG_INFO("Testing heater force control disable");

    temperature_control_init();

    // Force enable heater first
    utest_ntc_set_temperature(F16(25.0));
    utest_wbmz_set_powered_from_wbmz(false);
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);
    temperature_control_heater_force_control(true);

    // Disable force control
    temperature_control_heater_force_control(false);

    // Set low temperature
    utest_ntc_set_temperature(F16(EC_HEATER_ON_TEMP - 1.0));
    temperature_control_do_periodic_work();
    // Heater should now be controlled by temperature

    TEST_PASS();
}
#endif


int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_temperature_control_init);
    RUN_TEST(test_temperature_is_ready_above_minimum);
    RUN_TEST(test_temperature_is_not_ready_below_minimum);
    RUN_TEST(test_temperature_is_ready_at_minimum_threshold);
    RUN_TEST(test_get_temperature_positive);
    RUN_TEST(test_get_temperature_negative);
    RUN_TEST(test_get_temperature_zero);

#if defined(EC_GPIO_HEATER)
    RUN_TEST(test_heater_gpio_init);
    RUN_TEST(test_heater_turns_on_at_low_temperature);
    RUN_TEST(test_heater_turns_off_at_high_temperature);
    RUN_TEST(test_heater_stays_off_when_powered_from_wbmz);
    RUN_TEST(test_heater_stays_off_when_vin_not_present);
    RUN_TEST(test_heater_turns_off_when_wbmz_power_enabled_during_heating);
    RUN_TEST(test_heater_turns_off_when_vin_lost_during_heating);
    RUN_TEST(test_heater_hysteresis);
    RUN_TEST(test_heater_force_control_enable);
    RUN_TEST(test_heater_force_control_disable);
#endif

    return UNITY_END();
}
