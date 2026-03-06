#include "unity.h"
#include "hwrev.h"
#include "config.h"
#include "adc.h"
#include "mcu-pwr.h"
#include "fix16.h"
#include "utest_adc.h"
#include "utest_regmap.h"
#include "utest_systick.h"
#include "utest_wbmcu_system.h"
#include "utest_mcu_pwr.h"
#include "utest_wdt_stm32.h"
#include "utest_system_led.h"
#include "regmap-structs.h"
#include "regmap-int.h"
#include <setjmp.h>

#define LOG_LEVEL LOG_LEVEL_INFO
#include "console_log.h"

// Test helpers from hwrev_test_stubs.c
bool utest_rcc_set_hsi_pll_64mhz_clock_called(void);
bool utest_spi_slave_was_init_called(void);
void utest_hwrev_stubs_reset(void);

// Callback for watchdog_reload - increases time after first call
static void watchdog_reload_callback_trigger_reset(void)
{
    // After first watchdog_reload call, set time > 10000
    // so that NVIC_SystemReset will be called on next check
    if (utest_watchdog_get_reload_count() == 1) {
        utest_systick_set_time_ms(10001);
    }
}

// Calculate expected ADC value for revision
#define HWREV_ADC_VALUE_EXPECTED(res_up, res_down) \
    ((res_down) * 4096 / ((res_up) + (res_down)))

#define HWREV_ADC_VALUE_MIN(res_up, res_down) \
    HWREV_ADC_VALUE_EXPECTED(res_up, res_down) - \
    (HWREV_ADC_VALUE_EXPECTED(res_up, res_down) * WBEC_HWREV_DIFF_PERCENT / 100) - \
    WBEC_HWREV_DIFF_ADC

#define HWREV_ADC_VALUE_MAX(res_up, res_down) \
    HWREV_ADC_VALUE_EXPECTED(res_up, res_down) + \
    (HWREV_ADC_VALUE_EXPECTED(res_up, res_down) * WBEC_HWREV_DIFF_PERCENT / 100) + \
    WBEC_HWREV_DIFF_ADC

// Helper macro for getting revision code
#define __HWREV_CODE(hwrev_name, hwrev_code, res_up, res_down) hwrev_code,
static const uint16_t hwrev_codes[HWREV_COUNT] = {
    WBEC_HWREV_DESC(__HWREV_CODE)
};

// Helper macro for getting ADC values
#define __HWREV_ADC_VALUES(hwrev_name, hwrev_code, res_up, res_down) \
    HWREV_ADC_VALUE_EXPECTED(res_up, res_down),
static const int16_t hwrev_adc_values[HWREV_COUNT] = {
    WBEC_HWREV_DESC(__HWREV_ADC_VALUES)
};

// Helper macro for getting ADC min values
#define __HWREV_ADC_MIN_VALUES(hwrev_name, hwrev_code, res_up, res_down) \
    HWREV_ADC_VALUE_MIN(res_up, res_down),
static const int16_t hwrev_adc_min_values[HWREV_COUNT] = {
    WBEC_HWREV_DESC(__HWREV_ADC_MIN_VALUES)
};

// Helper macro for getting ADC max values
#define __HWREV_ADC_MAX_VALUES(hwrev_name, hwrev_code, res_up, res_down) \
    HWREV_ADC_VALUE_MAX(res_up, res_down),
static const int16_t hwrev_adc_max_values[HWREV_COUNT] = {
    WBEC_HWREV_DESC(__HWREV_ADC_MAX_VALUES)
};

void setUp(void)
{
    // Reset mock states
    utest_regmap_reset();
    utest_systick_set_time_ms(0);
    utest_systick_reset_init_flag();
    utest_nvic_reset();
    utest_mcu_reset();
    utest_watchdog_reset();
    utest_system_led_reset();
    utest_hwrev_stubs_reset();
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
}

void tearDown(void)
{

}


static void test_hwrev_get_default(void)
{
    LOG_INFO("Testing hwrev_get default value");

    // Before initialization, hwrev should be HWREV_UNKNOWN
    enum hwrev rev = hwrev_get();
    TEST_ASSERT_EQUAL_MESSAGE(HWREV_UNKNOWN, rev, "Before initialization, hwrev should be HWREV_UNKNOWN");
}


static void test_hwrev_init(void)
{
    LOG_INFO("Testing hwrev init for current model");

    // Set ADC value corresponding to current hardware
    utest_adc_set_ch_raw(ADC_CHANNEL_ADC_HW_VER, fix16_from_int(hwrev_adc_values[WBEC_HWREV]));

    hwrev_init_and_check();

    enum hwrev rev = hwrev_get();
    TEST_ASSERT_EQUAL_MESSAGE(WBEC_HWREV, rev, "After initialization, hwrev should match WBEC_HWREV");
}


static void test_hwrev_init_non_poweron(void)
{
    LOG_INFO("Testing hwrev init with non-POWER_ON reason (skip hwrev check)");

    // Set poweron reason to RTC_ALARM (not POWER_ON)
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_RTC_ALARM);

    // Set ADC value for WRONG hardware to verify that hwrev check is skipped
#ifdef MODEL_WB74
    enum hwrev wrong_hwrev = HWREV_WB85;
#elif defined(MODEL_WB85)
    enum hwrev wrong_hwrev = HWREV_WB74;
#else
    #error "Unknown model"
#endif
    utest_adc_set_ch_raw(ADC_CHANNEL_ADC_HW_VER, fix16_from_int(hwrev_adc_values[wrong_hwrev]));

    // Call hwrev_init_and_check - it should skip hwrev check and just set WBEC_HWREV
    hwrev_init_and_check();

    // Verify that hwrev was set to WBEC_HWREV (not checked against ADC)
    enum hwrev rev = hwrev_get();
    TEST_ASSERT_EQUAL_MESSAGE(WBEC_HWREV, rev, "With non-POWER_ON reason, hwrev should be set to WBEC_HWREV without checking ADC");

    // Read HW_INFO_PART1 to verify registers were filled correctly
    struct REGMAP_HW_INFO_PART1 hw_info_1;
    bool result = utest_regmap_get_region_data(REGMAP_REGION_HW_INFO_PART1, &hw_info_1, sizeof(hw_info_1));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read HW_INFO_PART1 region");

    // Check that hwrev_code is set to correct model code (not error)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(hwrev_codes[WBEC_HWREV], hw_info_1.hwrev_code, "hwrev_code should match current model code");

    // Check that hwrev_error_flag is 0 (no error)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, hw_info_1.hwrev_error_flag, "hwrev_error_flag should be 0 when hwrev check is skipped");

    // Read HW_INFO_PART2
    struct REGMAP_HW_INFO_PART2 hw_info_2;
    result = utest_regmap_get_region_data(REGMAP_REGION_HW_INFO_PART2, &hw_info_2, sizeof(hw_info_2));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read HW_INFO_PART2 region");

    // Check that hwrev_ok is set to WBEC_ID (success)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(WBEC_ID, hw_info_2.hwrev_ok, "hwrev_ok should be WBEC_ID when hwrev check is skipped");
}


static void test_hwrev_put_hw_info_to_regmap_correct(void)
{
    LOG_INFO("Testing hwrev_put_hw_info_to_regmap for correct revision");

    // Set ADC value corresponding to current hardware
    utest_adc_set_ch_raw(ADC_CHANNEL_ADC_HW_VER, fix16_from_int(hwrev_adc_values[WBEC_HWREV]));

    hwrev_init_and_check();
    hwrev_put_hw_info_to_regmap();

    // Read HW_INFO_PART1
    struct REGMAP_HW_INFO_PART1 hw_info_1;
    bool result = utest_regmap_get_region_data(REGMAP_REGION_HW_INFO_PART1, &hw_info_1, sizeof(hw_info_1));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read HW_INFO_PART1 region");

    // Check WBEC_ID
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(WBEC_ID, hw_info_1.wbec_id, "wbec_id should match WBEC_ID");

    // Check hwrev_code
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(hwrev_codes[WBEC_HWREV], hw_info_1.hwrev_code, "hwrev_code should match current model code");

    // Check fwrev fields
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(2, hw_info_1.fwrev_major, "fwrev_major should be 2");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3, hw_info_1.fwrev_minor, "fwrev_minor should be 3");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(5, hw_info_1.fwrev_patch, "fwrev_patch should be 5");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(7, hw_info_1.fwrev_suffix, "fwrev_suffix should be 7");

    // Check hwrev_error_flag (should be 0 for correct revision)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, hw_info_1.hwrev_error_flag, "hwrev_error_flag should be 0 for correct revision");

    // Read HW_INFO_PART2
    struct REGMAP_HW_INFO_PART2 hw_info_2;
    result = utest_regmap_get_region_data(REGMAP_REGION_HW_INFO_PART2, &hw_info_2, sizeof(hw_info_2));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read HW_INFO_PART2 region");

    // Check uid (from UID_BASE)
    TEST_ASSERT_EQUAL_UINT16_ARRAY_MESSAGE((uint16_t *)UID_BASE, hw_info_2.uid, 6, "uid array should match UID_BASE");

    // Check hwrev_ok (should be WBEC_ID for correct revision)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(WBEC_ID, hw_info_2.hwrev_ok, "hwrev_ok should be WBEC_ID for correct revision");
}


static void test_hwrev_put_hw_info_to_regmap_incorrect(void)
{
    LOG_INFO("Testing hwrev_put_hw_info_to_regmap for incorrect revision");

    // Set ADC value corresponding to different hardware (opposite of current model)
#ifdef MODEL_WB74
    enum hwrev wrong_hwrev = HWREV_WB85;
#elif defined(MODEL_WB85)
    enum hwrev wrong_hwrev = HWREV_WB74;
#else
    #error "Unknown model"
#endif

    utest_adc_set_ch_raw(ADC_CHANNEL_ADC_HW_VER, fix16_from_int(hwrev_adc_values[wrong_hwrev]));

    // Call hwrev_init_and_check() with POWER_ON to test real behavior
    // It will enter infinite loop, so we use setjmp/longjmp to exit when NVIC_SystemReset is called
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);

    // Start with time < 10 seconds to allow watchdog_reload to be called
    utest_systick_set_time_ms(5000);

    // Set callback that will trigger time > 10000 after first watchdog_reload
    utest_watchdog_set_reload_callback(watchdog_reload_callback_trigger_reset);

    // Use setjmp/longjmp to exit from infinite loop
    jmp_buf exit_jmp;
    utest_nvic_set_exit_jmp(&exit_jmp);

    if (setjmp(exit_jmp) == 0) {
        // This will detect hwrev mismatch, call hwrev_put_hw_info_to_regmap(),
        // enter infinite loop, and then call NVIC_SystemReset
        hwrev_init_and_check();

        // Should not reach here
        TEST_FAIL_MESSAGE("Should not reach this point - NVIC_SystemReset should be called");
    }

    // longjmp returned us here after NVIC_SystemReset was called
    // Now we can check that regmap was filled correctly
    utest_nvic_set_exit_jmp(NULL);

    // Verify that initialization functions were called during hwrev mismatch handling
    TEST_ASSERT_TRUE_MESSAGE(utest_rcc_set_hsi_pll_64mhz_clock_called(), "rcc_set_hsi_pll_64mhz_clock should be called on hwrev mismatch");
    TEST_ASSERT_TRUE_MESSAGE(utest_systick_was_init_called(), "systick_init should be called on hwrev mismatch");
    TEST_ASSERT_TRUE_MESSAGE(utest_spi_slave_was_init_called(), "spi_slave_init should be called on hwrev mismatch");
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_was_init_called(), "regmap_init should be called on hwrev mismatch");

    // Verify that watchdog_reload was called in the infinite loop
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, utest_watchdog_get_reload_count(), "watchdog_reload should be called at least once in the mismatch loop");

    // Verify that system_led_do_periodic_work was called in the infinite loop
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, utest_system_led_get_periodic_work_count(), "system_led_do_periodic_work should be called at least once in the mismatch loop");

    // Verify that LED is set to blink mode with correct parameters
    TEST_ASSERT_EQUAL_MESSAGE(UTEST_LED_MODE_BLINK, utest_system_led_get_mode(), "LED should be in BLINK mode on hwrev mismatch");
    uint16_t on_ms = 0, off_ms = 0;
    utest_system_led_get_blink_params(&on_ms, &off_ms);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(25, on_ms, "LED blink on time should be 25ms");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(25, off_ms, "LED blink off time should be 25ms");

    // Read HW_INFO_PART1
    struct REGMAP_HW_INFO_PART1 hw_info_1;
    bool result = utest_regmap_get_region_data(REGMAP_REGION_HW_INFO_PART1, &hw_info_1, sizeof(hw_info_1));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read HW_INFO_PART1 region");

    // Check WBEC_ID
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(WBEC_ID, hw_info_1.wbec_id, "wbec_id should match WBEC_ID");

    // When hwrev mismatch is detected, hwrev_code should be set to the detected (wrong) hwrev
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(hwrev_codes[wrong_hwrev], hw_info_1.hwrev_code, "hwrev_code should match detected (wrong) hwrev code");

    // Check fwrev fields
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(2, hw_info_1.fwrev_major, "fwrev_major should be 2");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3, hw_info_1.fwrev_minor, "fwrev_minor should be 3");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(5, hw_info_1.fwrev_patch, "fwrev_patch should be 5");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(7, hw_info_1.fwrev_suffix, "fwrev_suffix should be 7");

    // Check hwrev_error_flag (should be 0b1010 for hwrev mismatch)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0b1010, hw_info_1.hwrev_error_flag, "hwrev_error_flag should be 0b1010 for hwrev mismatch");

    // Read HW_INFO_PART2
    struct REGMAP_HW_INFO_PART2 hw_info_2;
    result = utest_regmap_get_region_data(REGMAP_REGION_HW_INFO_PART2, &hw_info_2, sizeof(hw_info_2));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read HW_INFO_PART2 region");

    // Check uid (from UID_BASE)
    TEST_ASSERT_EQUAL_UINT16_ARRAY_MESSAGE((uint16_t *)UID_BASE, hw_info_2.uid, 6, "uid array should match UID_BASE");

    // Check hwrev_ok (should be 0 for incorrect revision, not WBEC_ID)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, hw_info_2.hwrev_ok, "hwrev_ok should be 0 for incorrect revision");
}


static void test_hwrev_unknown_adc_value(void)
{
    LOG_INFO("Testing hwrev with unknown ADC value");

    // Set ADC value that doesn't correspond to any known revision
    // WB74: expected=0, range=-10..10
    // WB85: expected=738, range=706..770
    // 2000 is outside any known range
    utest_adc_set_ch_raw(ADC_CHANNEL_ADC_HW_VER, fix16_from_int(2000));

    // Call hwrev_init_and_check() with POWER_ON to test real behavior with unknown ADC value
    // It will detect unknown hwrev, enter infinite loop, so we use setjmp/longjmp to exit
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);

    // Start with time < 10 seconds to allow watchdog_reload to be called
    utest_systick_set_time_ms(5000);

    // Set callback that will trigger time > 10000 after first watchdog_reload
    utest_watchdog_set_reload_callback(watchdog_reload_callback_trigger_reset);

    // Use setjmp/longjmp to exit from infinite loop
    jmp_buf exit_jmp;
    utest_nvic_set_exit_jmp(&exit_jmp);

    if (setjmp(exit_jmp) == 0) {
        // This will detect unknown hwrev (doesn't match any known revision),
        // call hwrev_put_hw_info_to_regmap(), enter infinite loop, and call NVIC_SystemReset
        hwrev_init_and_check();

        // Should not reach here
        TEST_FAIL_MESSAGE("Should not reach this point - NVIC_SystemReset should be called");
    }

    // longjmp returned us here after NVIC_SystemReset was called
    utest_nvic_set_exit_jmp(NULL);

    // Verify that initialization functions were called during hwrev mismatch handling
    TEST_ASSERT_TRUE_MESSAGE(utest_rcc_set_hsi_pll_64mhz_clock_called(), "rcc_set_hsi_pll_64mhz_clock should be called on unknown hwrev");
    TEST_ASSERT_TRUE_MESSAGE(utest_systick_was_init_called(), "systick_init should be called on unknown hwrev");
    TEST_ASSERT_TRUE_MESSAGE(utest_spi_slave_was_init_called(), "spi_slave_init should be called on unknown hwrev");
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_was_init_called(), "regmap_init should be called on unknown hwrev");

    // Verify that watchdog_reload was called in the infinite loop
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, utest_watchdog_get_reload_count(), "watchdog_reload should be called at least once in the unknown hwrev loop");

    // Verify that system_led_do_periodic_work was called in the infinite loop
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, utest_system_led_get_periodic_work_count(), "system_led_do_periodic_work should be called at least once in the unknown hwrev loop");

    // Verify that LED is set to blink mode with correct parameters
    TEST_ASSERT_EQUAL_MESSAGE(UTEST_LED_MODE_BLINK, utest_system_led_get_mode(), "LED should be in BLINK mode on unknown hwrev");
    uint16_t on_ms = 0, off_ms = 0;
    utest_system_led_get_blink_params(&on_ms, &off_ms);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(25, on_ms, "LED blink on time should be 25ms");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(25, off_ms, "LED blink off time should be 25ms");

    // Verify that hwrev remained HWREV_UNKNOWN (no matching revision found)
    enum hwrev rev = hwrev_get();
    TEST_ASSERT_EQUAL_MESSAGE(HWREV_UNKNOWN, rev, "With unknown ADC value, hwrev should remain HWREV_UNKNOWN");

    // Read HW_INFO_PART1 to verify error flags
    struct REGMAP_HW_INFO_PART1 hw_info_1;
    bool result = utest_regmap_get_region_data(REGMAP_REGION_HW_INFO_PART1, &hw_info_1, sizeof(hw_info_1));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read HW_INFO_PART1 region");

    // Check WBEC_ID
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(WBEC_ID, hw_info_1.wbec_id, "wbec_id should match WBEC_ID");

    // Check hwrev_code (should be HWREV_UNKNOWN truncated to 12 bits)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE((HWREV_UNKNOWN & 0xFFF), hw_info_1.hwrev_code, "hwrev_code should be HWREV_UNKNOWN truncated to 12 bits for unknown hwrev");

    // Check fwrev fields
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(2, hw_info_1.fwrev_major, "fwrev_major should be 2");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3, hw_info_1.fwrev_minor, "fwrev_minor should be 3");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(5, hw_info_1.fwrev_patch, "fwrev_patch should be 5");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(7, hw_info_1.fwrev_suffix, "fwrev_suffix should be 7");

    // Check hwrev_error_flag (should be 0b1010 for hwrev mismatch/unknown)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0b1010, hw_info_1.hwrev_error_flag, "hwrev_error_flag should be 0b1010 for unknown hwrev");

    // Read HW_INFO_PART2
    struct REGMAP_HW_INFO_PART2 hw_info_2;
    result = utest_regmap_get_region_data(REGMAP_REGION_HW_INFO_PART2, &hw_info_2, sizeof(hw_info_2));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read HW_INFO_PART2 region");

    // Check uid (from UID_BASE)
    TEST_ASSERT_EQUAL_UINT16_ARRAY_MESSAGE((uint16_t *)UID_BASE, hw_info_2.uid, 6, "uid array should match UID_BASE");

    // Check hwrev_ok (should be 0 for unknown revision, not WBEC_ID)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, hw_info_2.hwrev_ok, "hwrev_ok should be 0 for unknown revision");
}

// Test that NVIC_SystemReset is called when hwrev mismatch is detected
static void test_hwrev_nvic_reset_on_mismatch(void)
{
    LOG_INFO("Testing NVIC_SystemReset call on hwrev mismatch");

    // Set POWER_ON so that hwrev check is active
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);

    // Set ADC value for different model (mismatch)
#ifdef MODEL_WB74
    enum hwrev wrong_hwrev = HWREV_WB85;
#elif defined(MODEL_WB85)
    enum hwrev wrong_hwrev = HWREV_WB74;
#else
    #error "Unknown model"
#endif

    utest_adc_set_ch_raw(ADC_CHANNEL_ADC_HW_VER, fix16_from_int(hwrev_adc_values[wrong_hwrev]));

    // Start with time < 10 seconds to allow watchdog_reload to be called
    utest_systick_set_time_ms(5000);

    // Set callback that will trigger time > 10000 after first watchdog_reload
    utest_watchdog_set_reload_callback(watchdog_reload_callback_trigger_reset);

    // Check that NVIC_SystemReset was not called before hwrev_init_and_check()
    TEST_ASSERT_FALSE_MESSAGE(utest_nvic_was_reset_called(), "NVIC_SystemReset should not be called before hwrev_init_and_check");

    // Use setjmp/longjmp to exit from infinite loop when NVIC_SystemReset is called
    jmp_buf exit_jmp;
    utest_nvic_set_exit_jmp(&exit_jmp);

    if (setjmp(exit_jmp) == 0) {
        // Call initialization - it will enter hwrev mismatch loop
        // and immediately call NVIC_SystemReset, since time is already > 10 seconds
        hwrev_init_and_check();

        // Should not reach here
        TEST_FAIL_MESSAGE("Should not reach this point - NVIC_SystemReset should be called");
    } else {
        // longjmp returned control here, meaning NVIC_SystemReset was called
        // Check that NVIC_SystemReset was indeed called
        TEST_ASSERT_TRUE_MESSAGE(utest_nvic_was_reset_called(), "NVIC_SystemReset should be called when hwrev mismatch detected");

        // Verify that initialization functions were called during hwrev mismatch handling
        TEST_ASSERT_TRUE_MESSAGE(utest_rcc_set_hsi_pll_64mhz_clock_called(), "rcc_set_hsi_pll_64mhz_clock should be called on hwrev mismatch");
        TEST_ASSERT_TRUE_MESSAGE(utest_systick_was_init_called(), "systick_init should be called on hwrev mismatch");
        TEST_ASSERT_TRUE_MESSAGE(utest_spi_slave_was_init_called(), "spi_slave_init should be called on hwrev mismatch");
        TEST_ASSERT_TRUE_MESSAGE(utest_regmap_was_init_called(), "regmap_init should be called on hwrev mismatch");

        // Verify that watchdog_reload was called in the infinite loop
        TEST_ASSERT_GREATER_THAN_MESSAGE(0, utest_watchdog_get_reload_count(), "watchdog_reload should be called at least once in the mismatch loop");

        // Verify that system_led_do_periodic_work was called in the infinite loop
        TEST_ASSERT_GREATER_THAN_MESSAGE(0, utest_system_led_get_periodic_work_count(), "system_led_do_periodic_work should be called at least once in the mismatch loop");

        // Verify that LED is set to blink mode with correct parameters
        TEST_ASSERT_EQUAL_MESSAGE(UTEST_LED_MODE_BLINK, utest_system_led_get_mode(), "LED should be in BLINK mode on hwrev mismatch");
        uint16_t on_ms = 0, off_ms = 0;
        utest_system_led_get_blink_params(&on_ms, &off_ms);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(25, on_ms, "LED blink on time should be 25ms");
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(25, off_ms, "LED blink off time should be 25ms");
    }
}


// Test hwrev detection at ADC range boundaries
static void test_hwrev_adc_range_min_boundary(void)
{
    LOG_INFO("Testing hwrev detection at adc_min boundary");

    // Set ADC value to exactly adc_min for current hardware
    utest_adc_set_ch_raw(ADC_CHANNEL_ADC_HW_VER, fix16_from_int(hwrev_adc_min_values[WBEC_HWREV]));

    hwrev_init_and_check();

    // Should be correctly detected at min boundary
    enum hwrev rev = hwrev_get();
    TEST_ASSERT_EQUAL_MESSAGE(WBEC_HWREV, rev, "hwrev should be detected at adc_min boundary");
}


static void test_hwrev_adc_range_max_boundary(void)
{
    LOG_INFO("Testing hwrev detection at adc_max boundary");

    // Set ADC value to exactly adc_max for current hardware
    utest_adc_set_ch_raw(ADC_CHANNEL_ADC_HW_VER, fix16_from_int(hwrev_adc_max_values[WBEC_HWREV]));

    hwrev_init_and_check();

    // Should be correctly detected at max boundary
    enum hwrev rev = hwrev_get();
    TEST_ASSERT_EQUAL_MESSAGE(WBEC_HWREV, rev, "hwrev should be detected at adc_max boundary");
}


static void test_hwrev_adc_range_below_min(void)
{
    LOG_INFO("Testing hwrev detection below adc_min boundary");

    // Set ADC value below adc_min for current hardware
    // This value should not match any known revision
    int16_t test_value = hwrev_adc_min_values[WBEC_HWREV] - 1;

    // Ensure the value doesn't fall into another revision's range
    for (int i = 0; i < HWREV_COUNT; i++) {
        if (i != WBEC_HWREV && test_value >= hwrev_adc_min_values[i] && test_value <= hwrev_adc_max_values[i]) {
            TEST_FAIL_MESSAGE("Test value falls into another revision's range, test is invalid");
            return;
        }
    }

    utest_adc_set_ch_raw(ADC_CHANNEL_ADC_HW_VER, fix16_from_int(test_value));

    // Call hwrev_init_and_check() - it should detect unknown hwrev and enter infinite loop
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_systick_set_time_ms(5000);
    utest_watchdog_set_reload_callback(watchdog_reload_callback_trigger_reset);

    jmp_buf exit_jmp;
    utest_nvic_set_exit_jmp(&exit_jmp);

    if (setjmp(exit_jmp) == 0) {
        hwrev_init_and_check();
        TEST_FAIL_MESSAGE("Should not reach this point - NVIC_SystemReset should be called");
    }

    utest_nvic_set_exit_jmp(NULL);

    // Should not be detected (remains HWREV_UNKNOWN)
    enum hwrev rev = hwrev_get();
    TEST_ASSERT_EQUAL_MESSAGE(HWREV_UNKNOWN, rev, "hwrev should remain HWREV_UNKNOWN when ADC value is below adc_min");
}


static void test_hwrev_adc_range_above_max(void)
{
    LOG_INFO("Testing hwrev detection above adc_max boundary");

    // Set ADC value above adc_max for current hardware
    // This value should not match any known revision
    int16_t test_value = hwrev_adc_max_values[WBEC_HWREV] + 1;

    // Ensure the value doesn't fall into another revision's range
    for (int i = 0; i < HWREV_COUNT; i++) {
        if (i != WBEC_HWREV && test_value >= hwrev_adc_min_values[i] && test_value <= hwrev_adc_max_values[i]) {
            TEST_FAIL_MESSAGE("Test value falls into another revision's range, test is invalid");
            return;
        }
    }

    utest_adc_set_ch_raw(ADC_CHANNEL_ADC_HW_VER, fix16_from_int(test_value));

    // Call hwrev_init_and_check() - it should detect unknown hwrev and enter infinite loop
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_systick_set_time_ms(5000);
    utest_watchdog_set_reload_callback(watchdog_reload_callback_trigger_reset);

    jmp_buf exit_jmp;
    utest_nvic_set_exit_jmp(&exit_jmp);

    if (setjmp(exit_jmp) == 0) {
        hwrev_init_and_check();
        TEST_FAIL_MESSAGE("Should not reach this point - NVIC_SystemReset should be called");
    }

    utest_nvic_set_exit_jmp(NULL);

    // Should not be detected as WBEC_HWREV
    enum hwrev rev = hwrev_get();
    TEST_ASSERT_EQUAL_MESSAGE(HWREV_UNKNOWN, rev, "hwrev should remain HWREV_UNKNOWN when ADC value is above adc_max");
}


int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_hwrev_get_default);
    RUN_TEST(test_hwrev_unknown_adc_value);     // Must run early while hwrev is still HWREV_UNKNOWN
    RUN_TEST(test_hwrev_adc_range_below_min);   // Must run early while hwrev is still HWREV_UNKNOWN
    RUN_TEST(test_hwrev_adc_range_above_max);   // Must run early while hwrev is still HWREV_UNKNOWN
    RUN_TEST(test_hwrev_init);
    RUN_TEST(test_hwrev_init_non_poweron);
    RUN_TEST(test_hwrev_adc_range_min_boundary);
    RUN_TEST(test_hwrev_adc_range_max_boundary);
    RUN_TEST(test_hwrev_put_hw_info_to_regmap_correct);
    RUN_TEST(test_hwrev_put_hw_info_to_regmap_incorrect);
    RUN_TEST(test_hwrev_nvic_reset_on_mismatch);

    return UNITY_END();
}
