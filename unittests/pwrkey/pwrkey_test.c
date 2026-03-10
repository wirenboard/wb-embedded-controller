#include "unity.h"
#include "pwrkey.h"
#include "config.h"
#include "systick.h"
#include "utest_systick.h"
#include "utest_wbmcu_system.h"
#include "utest_gpio.h"

#define LOG_LEVEL LOG_LEVEL_INFO
#include "console_log.h"

// PWRKEY GPIO pin (as defined in config)
static const gpio_pin_t pwrkey_gpio = { EC_GPIO_PWRKEY };

void setUp(void)
{
    // Reset all mock states
    utest_gpio_reset_instances();
    utest_systick_set_time_ms(1000);
    utest_pwr_reset();

    // Clear any pending press flags by calling handlers
    // This is needed because pwrkey.c uses static variables
    // that persist between tests
    pwrkey_handle_short_press();
    pwrkey_handle_long_press();
}

void tearDown(void)
{
}

static void test_pwrkey_init(void)
{
    LOG_INFO("Testing pwrkey initialization");

    pwrkey_init();

    // Check that PWRKEY GPIO is configured as input
    uint32_t mode = utest_gpio_get_mode(pwrkey_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(GPIO_MODE_INPUT, mode, "PWRKEY GPIO should be configured as INPUT");

    // Check PWR registers are configured correctly
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        // Check pull-up control register for port A
        uint32_t pucra_expected = (1U << pwrkey_gpio.pin);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(pucra_expected, PWR->PUCRA, "PWR->PUCRA should have pull-up set for PWRKEY pin");

        // Check falling edge trigger in CR4
        uint32_t cr4_expected = (1U << (EC_GPIO_PWRKEY_WKUP_NUM - 1));
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(cr4_expected, PWR->CR4, "PWR->CR4 should have falling edge trigger set");
    #elif EC_GPIO_PWRKEY_ACTIVE_HIGH
        // Check pull-down control register for port A
        uint32_t pdcra_expected = (1U << pwrkey_gpio.pin);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(pdcra_expected, PWR->PDCRA, "PWR->PDCRA should have pull-down set for PWRKEY pin");
    #endif

    // Check wakeup source in CR3
    uint32_t cr3_expected = (1U << (EC_GPIO_PWRKEY_WKUP_NUM - 1));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(cr3_expected, PWR->CR3, "PWR->CR3 should have wakeup source set for PWRKEY");
}

static void test_pwrkey_ready_after_debounce(void)
{
    LOG_INFO("Testing pwrkey ready state after debounce");

    pwrkey_init();

    // Simulate button released (active low)
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 1);  // High = released
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 0);  // Low = released
    #endif

    // Start periodic work to register initial state
    pwrkey_do_periodic_work();

    // Before debounce time elapsed, pwrkey should not be ready
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_ready(), "pwrkey should not be ready before debounce time");

    // Advance time but not enough for debounce
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS);
    pwrkey_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_ready(), "pwrkey should not be ready before debounce time elapsed");

    // Advance time to complete debounce
    utest_systick_advance_time_ms(1);
    pwrkey_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(pwrkey_ready(), "pwrkey should be ready after debounce time elapsed");
}

static void test_pwrkey_pressed_state(void)
{
    LOG_INFO("Testing pwrkey pressed state detection");

    pwrkey_init();

    // Simulate button released
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 1);  // High = released
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 0);  // Low = released
    #endif

    // Start periodic work
    pwrkey_do_periodic_work();

    // Wait for debounce
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS + 1);
    pwrkey_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(pwrkey_pressed(), "pwrkey_pressed() should return false when button is released");

    // Simulate button pressed
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 0);  // Low = pressed
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 1);  // High = pressed
    #endif

    // Start periodic work with new state
    pwrkey_do_periodic_work();

    // Wait for debounce
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS + 1);
    pwrkey_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(pwrkey_pressed(), "pwrkey_pressed() should return true when button is pressed");
}

static void test_pwrkey_debounce_on_press(void)
{
    LOG_INFO("Testing debounce on button press");

    pwrkey_init();

    // Start with button released
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 1);  // High = released
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 0);  // Low = released
    #endif

    // Start periodic work
    pwrkey_do_periodic_work();

    // Wait for initial debounce
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS + 1);
    pwrkey_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(pwrkey_pressed(), "Initial state should be released");

    // Simulate button press
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 0);  // Low = pressed
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 1);  // High = pressed
    #endif

    pwrkey_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_pressed(), "Button should not register as pressed immediately");

    // Advance time but not enough for debounce
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS);
    pwrkey_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_pressed(), "Button should not register as pressed before debounce time");

    // Advance time to complete debounce
    utest_systick_advance_time_ms(1);
    pwrkey_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(pwrkey_pressed(), "Button should register as pressed after debounce time");
}

static void test_pwrkey_debounce_glitch_rejection(void)
{
    LOG_INFO("Testing debounce glitch rejection");

    pwrkey_init();

    // Start with button released
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 1);  // High = released
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 0);  // Low = released
    #endif

    // Start periodic work
    pwrkey_do_periodic_work();

    // Wait for initial debounce
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS + 1);
    pwrkey_do_periodic_work();

    // Simulate a glitch (brief press)
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 0);  // Low = pressed
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 1);  // High = pressed
    #endif
    pwrkey_do_periodic_work();

    // Advance small amount of time
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS / 2);
    pwrkey_do_periodic_work();

    // Glitch ends - button released again
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 1);  // High = released
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 0);  // Low = released
    #endif
    pwrkey_do_periodic_work();

    // Advance time beyond glitch
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS);
    pwrkey_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(pwrkey_pressed(), "Button should still be released after glitch rejection");
}

static void test_pwrkey_short_press_detection(void)
{
    LOG_INFO("Testing short press detection");

    pwrkey_init();

    // Start with button released
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 1);  // High = released
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 0);  // Low = released
    #endif

    // Start periodic work
    pwrkey_do_periodic_work();

    // Wait for initial debounce
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS + 1);
    pwrkey_do_periodic_work();

    // No short press detected yet
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_short_press(), "No short press should be detected initially");

    // Simulate button press
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 0);  // Low = pressed
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 1);  // High = pressed
    #endif

    // Start periodic work with press
    pwrkey_do_periodic_work();

    // Wait for press debounce
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS + 1);
    pwrkey_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_short_press(), "No short press while button is held");

    // Hold for short duration (less than long press time)
    utest_systick_advance_time_ms(PWRKEY_LONG_PRESS_TIME_MS / 2);
    pwrkey_do_periodic_work();

    // Release button
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 1);  // High = released
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 0);  // Low = released
    #endif

    // Start periodic work with release
    pwrkey_do_periodic_work();

    // Wait for release debounce
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS + 1);
    pwrkey_do_periodic_work();

    // Short press should be detected
    TEST_ASSERT_TRUE_MESSAGE(pwrkey_handle_short_press(), "Short press should be detected after button release");

    // Flag should be cleared after handling
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_short_press(), "Short press flag should be cleared after handling");
}

static void test_pwrkey_long_press_detection(void)
{
    LOG_INFO("Testing long press detection");

    pwrkey_init();

    // Start with button released
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 1);  // High = released
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 0);  // Low = released
    #endif

    // Start periodic work
    pwrkey_do_periodic_work();

    // Wait for initial debounce
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS + 1);
    pwrkey_do_periodic_work();

    // No long press detected yet
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_long_press(), "No long press should be detected initially");

    // Simulate button press
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 0);  // Low = pressed
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 1);  // High = pressed
    #endif

    // Start periodic work with press
    pwrkey_do_periodic_work();

    // Wait for press debounce
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS + 1);
    pwrkey_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_long_press(), "No long press immediately after button press");

    // Hold for duration just before long press threshold
    utest_systick_advance_time_ms(PWRKEY_LONG_PRESS_TIME_MS - 1);
    pwrkey_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_long_press(), "No long press before threshold time");

    // Hold for one more millisecond to exactly reach threshold
    utest_systick_advance_time_ms(1);
    pwrkey_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_long_press(), "No long press at exact threshold (needs > not >=)");

    // Hold for one more millisecond to exceed threshold
    utest_systick_advance_time_ms(1);
    pwrkey_do_periodic_work();

    // Long press should be detected
    TEST_ASSERT_TRUE_MESSAGE(pwrkey_handle_long_press(), "Long press should be detected after threshold time");

    // Flag should be cleared after handling
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_long_press(), "Long press flag should be cleared after handling");
}

static void test_pwrkey_long_press_no_short_press(void)
{
    LOG_INFO("Testing that long press does not trigger short press");

    pwrkey_init();

    // Start with button released
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 1);  // High = released
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 0);  // Low = released
    #endif

    // Start periodic work
    pwrkey_do_periodic_work();

    // Wait for initial debounce
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS + 1);
    pwrkey_do_periodic_work();

    // Simulate button press
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 0);  // Low = pressed
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 1);  // High = pressed
    #endif

    // Start periodic work with press
    pwrkey_do_periodic_work();

    // Wait for press debounce
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS + 1);
    pwrkey_do_periodic_work();

    // Hold for long press duration
    utest_systick_advance_time_ms(PWRKEY_LONG_PRESS_TIME_MS + 1);
    pwrkey_do_periodic_work();

    // Long press should be detected
    TEST_ASSERT_TRUE_MESSAGE(pwrkey_handle_long_press(), "Long press should be detected");

    // Release button
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 1);  // High = released
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 0);  // Low = released
    #endif

    // Start periodic work with release
    pwrkey_do_periodic_work();

    // Wait for release debounce
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS + 1);
    pwrkey_do_periodic_work();

    // Short press should NOT be detected (since it was a long press)
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_short_press(), "Short press should NOT be detected after long press");
}

static void test_pwrkey_multiple_short_presses(void)
{
    LOG_INFO("Testing multiple short presses");

    pwrkey_init();

    // Start with button released
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 1);  // High = released
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 0);  // Low = released
    #endif

    // Start periodic work
    pwrkey_do_periodic_work();

    // Wait for initial debounce
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS + 1);
    pwrkey_do_periodic_work();

    // First short press
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 0);  // Press
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 1);
    #endif
    pwrkey_do_periodic_work();
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS + 1);
    pwrkey_do_periodic_work();

    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 1);  // Release
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 0);
    #endif
    pwrkey_do_periodic_work();
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS + 1);
    pwrkey_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(pwrkey_handle_short_press(), "First short press should be detected");

    // Second short press
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 0);  // Press
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 1);
    #endif
    pwrkey_do_periodic_work();
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS + 1);
    pwrkey_do_periodic_work();

    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 1);  // Release
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 0);
    #endif
    pwrkey_do_periodic_work();
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS + 1);
    pwrkey_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(pwrkey_handle_short_press(), "Second short press should be detected");
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_short_press(), "No more short presses should be pending");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_pwrkey_init);
    RUN_TEST(test_pwrkey_ready_after_debounce);
    RUN_TEST(test_pwrkey_pressed_state);
    RUN_TEST(test_pwrkey_debounce_on_press);
    RUN_TEST(test_pwrkey_debounce_glitch_rejection);
    RUN_TEST(test_pwrkey_short_press_detection);
    RUN_TEST(test_pwrkey_long_press_detection);
    RUN_TEST(test_pwrkey_long_press_no_short_press);
    RUN_TEST(test_pwrkey_multiple_short_presses);

    return UNITY_END();
}
