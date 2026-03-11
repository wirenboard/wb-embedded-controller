#include "unity.h"
#include "gpio-subsystem.h"
#include "config.h"
#include "utest_gpio.h"
#include "regmap-int.h"
#include "regmap-structs.h"
#include "utest_regmap.h"
#include "voltage-monitor.h"
#include "utest_voltage_monitor.h"
#include "bits.h"

#ifdef EC_MOD1_MOD2_GPIO_CONTROL
#include "shared-gpio.h"
#include "utest_shared_gpio.h"
#endif

#define LOG_LEVEL LOG_LEVEL_INFO
#include "console_log.h"

// GPIO enumeration matching gpio-subsytem.c
enum ec_ext_gpio {
    EC_EXT_GPIO_A1,
    EC_EXT_GPIO_A2,
    EC_EXT_GPIO_A3,
    EC_EXT_GPIO_A4,
    EC_EXT_GPIO_V_OUT,
    // Order of TX, RX, RTS must match the order in enum mod_gpio from shared-gpio.h
    EC_EXT_GPIO_MOD1_TX,
    EC_EXT_GPIO_MOD1_RX,
    EC_EXT_GPIO_MOD1_RTS,
    EC_EXT_GPIO_MOD2_TX,
    EC_EXT_GPIO_MOD2_RX,
    EC_EXT_GPIO_MOD2_RTS,

    EC_EXT_GPIO_COUNT
};

// Values in GPIO_AF region (2 bits per pin) matching gpio-subsytem.c
enum gpio_regmap_af {
    GPIO_REGMAP_AF_GPIO = 0,
    GPIO_REGMAP_AF_UART = 1,
};

#ifdef EC_MOD1_MOD2_GPIO_CONTROL
// AF index for MOD GPIOs (calculated as: mod * MOD_GPIO_COUNT + mod_gpio)
enum gpio_af_index {
    GPIO_AF_MOD1_TX = 0,
    GPIO_AF_MOD1_RX = 1,
    GPIO_AF_MOD1_RTS = 2,
    GPIO_AF_MOD2_TX = 3,
    GPIO_AF_MOD2_RX = 4,
    GPIO_AF_MOD2_RTS = 5,
};

// Calculate AF index from mod and mod_gpio (matching gpio-subsytem.c logic)
#define GPIO_AF_INDEX(mod, mod_gpio) ((mod) * MOD_GPIO_COUNT + (mod_gpio))

// Helper macro to set AF value for specific MOD GPIO
#define GPIO_AF_SET(gpio_af_index, af_value) ((af_value) << ((gpio_af_index) * 2))
#endif

// V_OUT GPIO pin
static const gpio_pin_t v_out_gpio = { EC_GPIO_VOUT_EN };

void setUp(void)
{
    // Reset all mock states
    utest_gpio_reset_instances();
    utest_regmap_reset();
    utest_vmon_reset();

#ifdef EC_MOD1_MOD2_GPIO_CONTROL
    utest_shared_gpio_reset();
#endif
}

void tearDown(void)
{

}


static void test_gpio_init(void)
{
    LOG_INFO("Testing GPIO initialization");

    gpio_init();

    // Check that V_OUT GPIO is configured as output
    uint32_t mode = utest_gpio_get_mode(v_out_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(GPIO_MODE_OUTPUT, mode, "V_OUT GPIO should be configured as OUTPUT");

    // Check that V_OUT GPIO is configured as push-pull
    uint32_t otype = utest_gpio_get_output_type(v_out_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(GPIO_OTYPE_PUSH_PULL, otype, "V_OUT GPIO should be configured as PUSH-PULL");

    // Check that V_OUT is off after init
    uint32_t state = utest_gpio_get_output_state(v_out_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "V_OUT GPIO should be LOW (off) after initialization");

#ifdef EC_MOD1_MOD2_GPIO_CONTROL
    // Check that shared_gpio was initialized (all GPIOs should be in INPUT mode)
    for (unsigned mod = 0; mod < MOD_COUNT; mod++) {
        for (unsigned gpio = 0; gpio < MOD_GPIO_COUNT; gpio++) {
            enum mod_gpio_mode gpio_mode = utest_shared_gpio_get_mode(mod, gpio);
            TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_INPUT, gpio_mode, "All shared GPIOs should be in INPUT mode after initialization");
        }
    }
#endif
}


static void test_gpio_reset(void)
{
    LOG_INFO("Testing GPIO reset");

    gpio_init();
    gpio_reset();

    // After reset, check that regmap regions are initialized
    struct REGMAP_GPIO_CTRL gpio_ctrl;
    bool result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_CTRL region after reset");

    struct REGMAP_GPIO_DIR gpio_dir;
    result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_DIR region after reset");

    struct REGMAP_GPIO_AF gpio_af;
    result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_AF, &gpio_af, sizeof(gpio_af));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_AF region after reset");

#ifdef EC_MOD1_MOD2_GPIO_CONTROL
    // After reset, all MOD GPIOs should be in INPUT mode
    for (unsigned mod = 0; mod < MOD_COUNT; mod++) {
        for (unsigned gpio = 0; gpio < MOD_GPIO_COUNT; gpio++) {
            enum mod_gpio_mode mode = utest_shared_gpio_get_mode(mod, gpio);
            TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_INPUT, mode, "All MOD GPIOs should be in INPUT mode after reset");
        }
    }
#endif
}


#ifdef EC_MOD1_MOD2_GPIO_CONTROL
static void test_gpio_direction_change_input_to_output(void)
{
    LOG_INFO("Testing GPIO direction change from INPUT to OUTPUT");

    gpio_init();
    gpio_reset();

    // Set MOD2_RX as output
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD2_RX);
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);

    gpio_do_periodic_work();

    // Check that MOD2_RX mode changed to OUTPUT
    enum mod_gpio_mode mode = utest_shared_gpio_get_mode(MOD2, MOD_GPIO_RX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_OUTPUT, mode, "MOD2_RX should be in OUTPUT mode");
}


static void test_gpio_direction_change_output_to_input(void)
{
    LOG_INFO("Testing GPIO direction change from OUTPUT to INPUT");

    gpio_init();
    gpio_reset();

    // First set MOD1_RTS as output
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD1_RTS);
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Now change back to input
    gpio_dir.gpio_dir = 0;
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Check that MOD1_RTS mode changed back to INPUT
    enum mod_gpio_mode mode = utest_shared_gpio_get_mode(MOD1, MOD_GPIO_RTS);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_INPUT, mode, "MOD1_RTS should be in INPUT mode");
}


static void test_gpio_set_output_value(void)
{
    LOG_INFO("Testing GPIO output value setting");

    gpio_init();
    gpio_reset();

    // Set MOD2_TX as output
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD2_TX);
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Set output value to HIGH
    struct REGMAP_GPIO_CTRL gpio_ctrl;
    gpio_ctrl.gpio_ctrl = BIT(EC_EXT_GPIO_MOD2_TX);  // MOD2_TX HIGH
    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);
    gpio_do_periodic_work();

    // Check output value
    bool value = utest_shared_gpio_get_output_value(MOD2, MOD_GPIO_TX);
    TEST_ASSERT_TRUE_MESSAGE(value, "MOD2_TX output should be HIGH");

    // Set output value to LOW
    gpio_ctrl.gpio_ctrl = 0;
    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);
    gpio_do_periodic_work();

    // Check output value
    value = utest_shared_gpio_get_output_value(MOD2, MOD_GPIO_TX);
    TEST_ASSERT_FALSE_MESSAGE(value, "MOD2_TX output should be LOW");
}


static void test_gpio_read_input_value(void)
{
    LOG_INFO("Testing GPIO input value reading");

    gpio_init();
    gpio_reset();

    // MOD1_RX is in INPUT mode by default after reset
    // Set input value to HIGH
    utest_shared_gpio_set_input_value(MOD1, MOD_GPIO_RX, true);

    gpio_do_periodic_work();

    // Read back GPIO_CTRL to check input state
    struct REGMAP_GPIO_CTRL gpio_ctrl;
    bool result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_CTRL");
    TEST_ASSERT_TRUE_MESSAGE(gpio_ctrl.gpio_ctrl & BIT(EC_EXT_GPIO_MOD1_RX), "MOD1_RX input should be HIGH");

    // Set input value to LOW
    utest_shared_gpio_set_input_value(MOD1, MOD_GPIO_RX, false);
    gpio_do_periodic_work();

    result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_CTRL");
    TEST_ASSERT_FALSE_MESSAGE(gpio_ctrl.gpio_ctrl & BIT(EC_EXT_GPIO_MOD1_RX), "MOD1_RX input should be LOW");
}


static void test_gpio_af_mode_uart(void)
{
    LOG_INFO("Testing GPIO AF mode - UART");

    gpio_init();
    gpio_reset();

    // Set MOD1_TX to UART mode
    struct REGMAP_GPIO_AF gpio_af;
    gpio_af.af = GPIO_AF_SET(GPIO_AF_MOD1_TX, GPIO_REGMAP_AF_UART);
    regmap_set_region_data(REGMAP_REGION_GPIO_AF, &gpio_af, sizeof(gpio_af));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_AF);
    gpio_do_periodic_work();

    // Check that MOD1_TX is in UART mode
    enum mod_gpio_mode mode = utest_shared_gpio_get_mode(MOD1, MOD_GPIO_TX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_AF_UART, mode, "MOD1_TX should be in UART AF mode");
}


static void test_gpio_af_mode_prevents_direction_change(void)
{
    LOG_INFO("Testing that AF mode prevents direction change");

    gpio_init();
    gpio_reset();

    // Set MOD2_RX to UART mode
    struct REGMAP_GPIO_AF gpio_af;
    gpio_af.af = GPIO_AF_SET(GPIO_AF_MOD2_RX, GPIO_REGMAP_AF_UART);
    regmap_set_region_data(REGMAP_REGION_GPIO_AF, &gpio_af, sizeof(gpio_af));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_AF);
    gpio_do_periodic_work();

    // Try to change direction to OUTPUT
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD2_RX);
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Mode should still be UART, not OUTPUT
    enum mod_gpio_mode mode = utest_shared_gpio_get_mode(MOD2, MOD_GPIO_RX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_AF_UART, mode, "MOD2_RX should remain in UART AF mode");
}


static void test_gpio_output_value_preserved_on_direction_change(void)
{
    LOG_INFO("Testing that output value is preserved when changing direction");

    gpio_init();
    gpio_reset();

    // Step 1: Set desired output value while pin is still input
    struct REGMAP_GPIO_CTRL gpio_ctrl;
    gpio_ctrl.gpio_ctrl = BIT(EC_EXT_GPIO_MOD2_RTS);  // MOD2_RTS = HIGH
    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);
    gpio_do_periodic_work();

    // Step 2: Change direction to output
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD2_RTS);  // MOD2_RTS = OUTPUT
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Check that output value was set to HIGH as requested
    bool value = utest_shared_gpio_get_output_value(MOD2, MOD_GPIO_RTS);
    TEST_ASSERT_TRUE_MESSAGE(value, "MOD2_RTS output should be HIGH after direction change");
}


static void test_gpio_output_value_preserved_without_request(void)
{
    LOG_INFO("Testing that output value is preserved without prior request");

    gpio_init();
    gpio_reset();

    // Set MOD1_TX as input with HIGH value
    utest_shared_gpio_set_input_value(MOD1, MOD_GPIO_TX, true);
    gpio_do_periodic_work();

    // Now change to output WITHOUT setting gpio_ctrl first
    // The current input value should be preserved
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD1_TX);
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Check that output value matches the previous input value (HIGH)
    bool value = utest_shared_gpio_get_output_value(MOD1, MOD_GPIO_TX);
    TEST_ASSERT_TRUE_MESSAGE(value, "MOD1_TX output should preserve input HIGH value");
}


static void test_gpio_af_mode_clears_ctrl_bit(void)
{
    LOG_INFO("Testing that AF mode clears gpio_ctrl bit when direction changes");

    gpio_init();
    gpio_reset();

    // Set MOD1_RX to HIGH while in input mode
    utest_shared_gpio_set_input_value(MOD1, MOD_GPIO_RX, true);
    gpio_do_periodic_work();

    // Verify gpio_ctrl has the bit set
    struct REGMAP_GPIO_CTRL gpio_ctrl;
    bool result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_CTRL");
    TEST_ASSERT_TRUE_MESSAGE(gpio_ctrl.gpio_ctrl & BIT(EC_EXT_GPIO_MOD1_RX), "MOD1_RX bit should be set initially");

    // Set MOD1_RX to UART mode
    struct REGMAP_GPIO_AF gpio_af;
    gpio_af.af = GPIO_AF_SET(GPIO_AF_MOD1_RX, GPIO_REGMAP_AF_UART);
    regmap_set_region_data(REGMAP_REGION_GPIO_AF, &gpio_af, sizeof(gpio_af));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_AF);
    gpio_do_periodic_work();

    // Try to change direction
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD1_RX);
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // gpio_ctrl bit should be cleared for AF mode pin
    result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_CTRL");
    TEST_ASSERT_FALSE_MESSAGE(gpio_ctrl.gpio_ctrl & BIT(EC_EXT_GPIO_MOD1_RX), "MOD1_RX bit should be cleared in AF mode");
}


static void test_gpio_dir_preserves_v_out_as_output(void)
{
    LOG_INFO("Testing that V_OUT always remains as output");

    gpio_init();
    gpio_reset();

    // Try to set all GPIOs as inputs (including V_OUT)
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = 0;  // Try to set all as inputs
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Read back GPIO_DIR - V_OUT bit should still be set
    bool result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_DIR");
    TEST_ASSERT_TRUE_MESSAGE(gpio_dir.gpio_dir & BIT(EC_EXT_GPIO_V_OUT), "V_OUT should always be output");
}


static void test_gpio_af_switches_mode_based_on_direction(void)
{
    LOG_INFO("Testing AF mode switches between INPUT/OUTPUT based on direction");

    gpio_init();
    gpio_reset();

    // Set MOD2_TX to GPIO mode (AF = 0) and as OUTPUT
    struct REGMAP_GPIO_AF gpio_af;
    gpio_af.af = GPIO_AF_SET(GPIO_AF_MOD2_TX, GPIO_REGMAP_AF_GPIO);
    regmap_set_region_data(REGMAP_REGION_GPIO_AF, &gpio_af, sizeof(gpio_af));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_AF);

    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD2_TX);
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Should be OUTPUT
    enum mod_gpio_mode mode = utest_shared_gpio_get_mode(MOD2, MOD_GPIO_TX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_OUTPUT, mode, "MOD2_TX should be OUTPUT");

    // Change direction to INPUT (keeping AF = GPIO)
    gpio_dir.gpio_dir = 0;
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Should be INPUT now
    mode = utest_shared_gpio_get_mode(MOD2, MOD_GPIO_TX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_INPUT, mode, "MOD2_TX should be INPUT");
}


static void test_gpio_values_only_set_for_changed_pins(void)
{
    LOG_INFO("Testing that GPIO values are only set for changed pins");

    gpio_init();
    gpio_reset();

    // Set MOD1_TX and MOD2_RX as outputs
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD1_TX) | BIT(EC_EXT_GPIO_MOD2_RX);
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Set both to LOW initially
    struct REGMAP_GPIO_CTRL gpio_ctrl;
    gpio_ctrl.gpio_ctrl = 0;
    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);
    gpio_do_periodic_work();

    // Change only MOD1_TX to HIGH (MOD2_RX stays LOW)
    gpio_ctrl.gpio_ctrl = BIT(EC_EXT_GPIO_MOD1_TX);
    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);
    gpio_do_periodic_work();

    // MOD1_TX should be HIGH
    bool value = utest_shared_gpio_get_output_value(MOD1, MOD_GPIO_TX);
    TEST_ASSERT_TRUE_MESSAGE(value, "MOD1_TX should be HIGH");

    // MOD2_RX should still be LOW (unchanged)
    value = utest_shared_gpio_get_output_value(MOD2, MOD_GPIO_RX);
    TEST_ASSERT_FALSE_MESSAGE(value, "MOD2_RX should be LOW");
}


static void test_gpio_collect_only_reads_inputs(void)
{
    LOG_INFO("Testing that collect_gpio_states only reads INPUT pins");

    gpio_init();
    gpio_reset();

    // Set MOD1_TX as OUTPUT with value HIGH
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD1_TX);
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);

    struct REGMAP_GPIO_CTRL gpio_ctrl;
    gpio_ctrl.gpio_ctrl = BIT(EC_EXT_GPIO_MOD1_TX);
    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);
    gpio_do_periodic_work();

    // Set MOD1_RX as INPUT with value HIGH
    utest_shared_gpio_set_input_value(MOD1, MOD_GPIO_RX, true);

    // Simulate physical pin state different from output value for MOD1_TX
    // (this should be ignored since it's an output)
    utest_shared_gpio_set_input_value(MOD1, MOD_GPIO_TX, false);

    gpio_do_periodic_work();

    // Read back GPIO_CTRL
    bool result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_CTRL");

    // MOD1_RX (INPUT) should reflect input state (HIGH)
    TEST_ASSERT_TRUE_MESSAGE(gpio_ctrl.gpio_ctrl & BIT(EC_EXT_GPIO_MOD1_RX), "MOD1_RX input should be HIGH");

    // MOD1_TX (OUTPUT) should keep its output value (HIGH), not read from input
    TEST_ASSERT_TRUE_MESSAGE(gpio_ctrl.gpio_ctrl & BIT(EC_EXT_GPIO_MOD1_TX), "MOD1_TX should keep output value HIGH");
}


static void test_gpio_collect_ignores_af_pins(void)
{
    LOG_INFO("Testing that collect_gpio_states ignores AF mode pins");

    gpio_init();
    gpio_reset();

    // Set MOD1_RX to UART mode
    struct REGMAP_GPIO_AF gpio_af;
    gpio_af.af = GPIO_AF_SET(GPIO_AF_MOD1_RX, GPIO_REGMAP_AF_UART);
    regmap_set_region_data(REGMAP_REGION_GPIO_AF, &gpio_af, sizeof(gpio_af));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_AF);
    gpio_do_periodic_work();

    // Set input value for MOD1_RX (should be ignored since it's in AF mode)
    utest_shared_gpio_set_input_value(MOD1, MOD_GPIO_RX, true);

    // Set MOD1_TX as INPUT with HIGH (should be read)
    utest_shared_gpio_set_input_value(MOD1, MOD_GPIO_TX, true);

    gpio_do_periodic_work();

    // Read back GPIO_CTRL
    struct REGMAP_GPIO_CTRL gpio_ctrl;
    bool result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_CTRL");

    // MOD1_TX (INPUT in GPIO mode) should be HIGH
    TEST_ASSERT_TRUE_MESSAGE(gpio_ctrl.gpio_ctrl & BIT(EC_EXT_GPIO_MOD1_TX), "MOD1_TX input should be HIGH");

    // MOD1_RX (AF mode) should be cleared (not reading input)
    TEST_ASSERT_FALSE_MESSAGE(gpio_ctrl.gpio_ctrl & BIT(EC_EXT_GPIO_MOD1_RX), "MOD1_RX should be cleared in AF mode");
}


static void test_gpio_af_ignores_reserved_values(void)
{
    LOG_INFO("Testing that undefined AF values (2, 3) are ignored");

    gpio_init();
    gpio_reset();

    // Set MOD1_TX to INPUT mode initially
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = 0;
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Verify it's INPUT
    enum mod_gpio_mode mode = utest_shared_gpio_get_mode(MOD1, MOD_GPIO_TX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_INPUT, mode, "MOD1_TX should be INPUT initially");

    // Set AF to reserved value 2 (neither GPIO nor UART)
    struct REGMAP_GPIO_AF gpio_af;
    gpio_af.af = GPIO_AF_SET(GPIO_AF_MOD1_TX, 2);  // Reserved value
    regmap_set_region_data(REGMAP_REGION_GPIO_AF, &gpio_af, sizeof(gpio_af));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_AF);
    gpio_do_periodic_work();

    // Mode should remain unchanged (INPUT) - reserved AF value is ignored
    mode = utest_shared_gpio_get_mode(MOD1, MOD_GPIO_TX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_INPUT, mode, "MOD1_TX should remain INPUT when AF=2 (reserved)");

    // Try another reserved value (3)
    gpio_af.af = GPIO_AF_SET(GPIO_AF_MOD1_TX, 3);  // Reserved value
    regmap_set_region_data(REGMAP_REGION_GPIO_AF, &gpio_af, sizeof(gpio_af));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_AF);
    gpio_do_periodic_work();

    // Mode should still remain unchanged
    mode = utest_shared_gpio_get_mode(MOD1, MOD_GPIO_TX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_INPUT, mode, "MOD1_TX should remain INPUT when AF=3 (reserved)");
}
#endif


static void test_v_out_control_enabled_when_power_ok(void)
{
    LOG_INFO("Testing V_OUT control - enabled when power OK");

    gpio_init();
    gpio_reset();

    // Set V_OUT to ON in GPIO_CTRL
    struct REGMAP_GPIO_CTRL gpio_ctrl;
    gpio_ctrl.gpio_ctrl = BIT(EC_EXT_GPIO_V_OUT);
    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);

    // Set V_OUT voltage monitor status to OK
    utest_vmon_set_ch_status(VMON_CHANNEL_V_OUT, true);

    gpio_do_periodic_work();

    // Check that V_OUT GPIO is HIGH
    uint32_t state = utest_gpio_get_output_state(v_out_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "V_OUT GPIO should be HIGH when power is OK");
}


static void test_v_out_control_disabled_when_power_not_ok(void)
{
    LOG_INFO("Testing V_OUT control - disabled when power not OK");

    gpio_init();
    gpio_reset();

    // Set V_OUT to ON in GPIO_CTRL
    struct REGMAP_GPIO_CTRL gpio_ctrl;
    gpio_ctrl.gpio_ctrl = BIT(EC_EXT_GPIO_V_OUT);
    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);

    // Set V_OUT voltage monitor status to NOT OK
    utest_vmon_set_ch_status(VMON_CHANNEL_V_OUT, false);

    gpio_do_periodic_work();

    // Check that V_OUT GPIO is LOW
    uint32_t state = utest_gpio_get_output_state(v_out_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "V_OUT GPIO should be LOW when power is not OK");
}


static void test_v_out_control_disabled_when_ctrl_off(void)
{
    LOG_INFO("Testing V_OUT control - disabled when CTRL is OFF");

    gpio_init();
    gpio_reset();

    // Set V_OUT to OFF in GPIO_CTRL
    struct REGMAP_GPIO_CTRL gpio_ctrl;
    gpio_ctrl.gpio_ctrl = 0;  // V_OUT OFF
    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);

    // Set V_OUT voltage monitor status to OK
    utest_vmon_set_ch_status(VMON_CHANNEL_V_OUT, true);

    gpio_do_periodic_work();

    // Check that V_OUT GPIO is LOW
    uint32_t state = utest_gpio_get_output_state(v_out_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "V_OUT GPIO should be LOW when CTRL is OFF");
}


static void test_v_out_control_requires_both_conditions(void)
{
    LOG_INFO("Testing V_OUT control - requires both power OK and CTRL ON");

    gpio_init();
    gpio_reset();

    // Test all combinations
    struct {
        bool ctrl_on;
        bool power_ok;
        uint32_t expected_state;
    } test_cases[] = {
        { false, false, 0 },  // Both OFF -> V_OUT OFF
        { false, true,  0 },  // CTRL OFF, power OK -> V_OUT OFF
        { true,  false, 0 },  // CTRL ON, power NOT OK -> V_OUT OFF
        { true,  true,  1 },  // Both ON -> V_OUT ON
    };

    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        struct REGMAP_GPIO_CTRL gpio_ctrl;
        gpio_ctrl.gpio_ctrl = test_cases[i].ctrl_on ? BIT(EC_EXT_GPIO_V_OUT) : 0;
        regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
        utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);

        utest_vmon_set_ch_status(VMON_CHANNEL_V_OUT, test_cases[i].power_ok);

        gpio_do_periodic_work();

        uint32_t state = utest_gpio_get_output_state(v_out_gpio);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(test_cases[i].expected_state, state,
            "V_OUT state mismatch in test case");
    }
}


#ifdef EC_MOD1_MOD2_GPIO_CONTROL
static void test_multiple_gpios_independent(void)
{
    LOG_INFO("Testing multiple GPIOs independence");

    gpio_init();
    gpio_reset();

    // Set MOD1_TX and MOD2_RTS as outputs, others as inputs
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD1_TX) | BIT(EC_EXT_GPIO_MOD2_RTS);
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);

    struct REGMAP_GPIO_CTRL gpio_ctrl;
    gpio_ctrl.gpio_ctrl = BIT(EC_EXT_GPIO_MOD1_TX);  // MOD1_TX HIGH, MOD2_RTS LOW
    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);

    // Set MOD1_RX and MOD2_RX inputs to HIGH
    utest_shared_gpio_set_input_value(MOD1, MOD_GPIO_RX, true);
    utest_shared_gpio_set_input_value(MOD2, MOD_GPIO_RX, true);

    gpio_do_periodic_work();

    // Check MOD1_TX (output HIGH)
    enum mod_gpio_mode mode = utest_shared_gpio_get_mode(MOD1, MOD_GPIO_TX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_OUTPUT, mode, "MOD1_TX should be OUTPUT");
    bool value = utest_shared_gpio_get_output_value(MOD1, MOD_GPIO_TX);
    TEST_ASSERT_TRUE_MESSAGE(value, "MOD1_TX should be HIGH");

    // Check MOD2_RTS (output LOW)
    mode = utest_shared_gpio_get_mode(MOD2, MOD_GPIO_RTS);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_OUTPUT, mode, "MOD2_RTS should be OUTPUT");
    value = utest_shared_gpio_get_output_value(MOD2, MOD_GPIO_RTS);
    TEST_ASSERT_FALSE_MESSAGE(value, "MOD2_RTS should be LOW");

    // Check MOD1_RX (input HIGH)
    mode = utest_shared_gpio_get_mode(MOD1, MOD_GPIO_RX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_INPUT, mode, "MOD1_RX should be INPUT");

    // Check MOD2_RX (input HIGH)
    mode = utest_shared_gpio_get_mode(MOD2, MOD_GPIO_RX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_INPUT, mode, "MOD2_RX should be INPUT");

    // Read back GPIO_CTRL to verify inputs
    bool result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_CTRL");
    TEST_ASSERT_TRUE_MESSAGE(gpio_ctrl.gpio_ctrl & BIT(EC_EXT_GPIO_MOD1_RX), "MOD1_RX input should be HIGH");
    TEST_ASSERT_TRUE_MESSAGE(gpio_ctrl.gpio_ctrl & BIT(EC_EXT_GPIO_MOD2_RX), "MOD2_RX input should be HIGH");
}
#endif


int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_gpio_init);
    RUN_TEST(test_gpio_reset);

#ifdef EC_MOD1_MOD2_GPIO_CONTROL
    RUN_TEST(test_gpio_direction_change_input_to_output);
    RUN_TEST(test_gpio_direction_change_output_to_input);
    RUN_TEST(test_gpio_set_output_value);
    RUN_TEST(test_gpio_read_input_value);
    RUN_TEST(test_gpio_af_mode_uart);
    RUN_TEST(test_gpio_af_mode_prevents_direction_change);
    RUN_TEST(test_gpio_output_value_preserved_on_direction_change);
    RUN_TEST(test_gpio_output_value_preserved_without_request);
    RUN_TEST(test_gpio_af_mode_clears_ctrl_bit);
    RUN_TEST(test_gpio_dir_preserves_v_out_as_output);
    RUN_TEST(test_gpio_af_switches_mode_based_on_direction);
    RUN_TEST(test_gpio_values_only_set_for_changed_pins);
    RUN_TEST(test_gpio_collect_only_reads_inputs);
    RUN_TEST(test_gpio_collect_ignores_af_pins);
    RUN_TEST(test_gpio_af_ignores_reserved_values);
    RUN_TEST(test_multiple_gpios_independent);
#endif

    RUN_TEST(test_v_out_control_enabled_when_power_ok);
    RUN_TEST(test_v_out_control_disabled_when_power_not_ok);
    RUN_TEST(test_v_out_control_disabled_when_ctrl_off);
    RUN_TEST(test_v_out_control_requires_both_conditions);

    return UNITY_END();
}
