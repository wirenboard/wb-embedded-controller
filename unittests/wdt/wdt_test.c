#include "unity.h"
#include "wdt.h"
#include "config.h"
#include "systick.h"
#include "regmap-int.h"
#include "utest_systick.h"
#include "utest_regmap.h"

#define LOG_LEVEL LOG_LEVEL_INFO
#include "console_log.h"

void setUp(void)
{
    // Reset all mock states
    utest_systick_set_time_ms(1000);
    utest_regmap_reset();

    // Clear any pending timed_out flags
    wdt_handle_timed_out();
}

void tearDown(void)
{
}

// Scenario: Set watchdog timeout with normal values (10s, 120s)
// Expected: Timeout values stored in regmap correctly
static void test_wdt_set_timeout_normal(void)
{
    LOG_INFO("Testing wdt_set_timeout with normal values");

    // Test setting various normal timeout values
    wdt_set_timeout(10);
    wdt_do_periodic_work();

    struct REGMAP_WDT w;
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(10, w.timeout, "Timeout should be set to 10 seconds");

    wdt_set_timeout(120);
    wdt_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(120, w.timeout, "Timeout should be set to 120 seconds");
}

// Scenario: Set watchdog timeout to 0
// Expected: Timeout clamped to minimum value of 1 second
static void test_wdt_set_timeout_zero(void)
{
    LOG_INFO("Testing wdt_set_timeout with zero");

    wdt_set_timeout(0);
    wdt_do_periodic_work();

    struct REGMAP_WDT w;
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, w.timeout,
                                     "Timeout 0 should be clamped to 1 second");
}

// Scenario: Set watchdog timeout to maximum allowed value
// Expected: Timeout set to WBEC_WATCHDOG_MAX_TIMEOUT_S without clamping
static void test_wdt_set_timeout_max(void)
{
    LOG_INFO("Testing wdt_set_timeout with max value");

    wdt_set_timeout(WBEC_WATCHDOG_MAX_TIMEOUT_S);
    wdt_do_periodic_work();

    struct REGMAP_WDT w;
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(WBEC_WATCHDOG_MAX_TIMEOUT_S, w.timeout,
                                     "Timeout should be set to max value");
}

// Scenario: Set watchdog timeout above maximum allowed value
// Expected: Timeout clamped to WBEC_WATCHDOG_MAX_TIMEOUT_S
static void test_wdt_set_timeout_over_max(void)
{
    LOG_INFO("Testing wdt_set_timeout with value over max");

    wdt_set_timeout(WBEC_WATCHDOG_MAX_TIMEOUT_S + 100);
    wdt_do_periodic_work();

    struct REGMAP_WDT w;
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(WBEC_WATCHDOG_MAX_TIMEOUT_S, w.timeout,
                                     "Timeout over max should be clamped to max value");
}

// Scenario: Start watchdog, wait less than timeout, reset it, then wait again
// Expected: Watchdog does not timeout after first period due to reset;
// times out after second period from reset point
static void test_wdt_start_reset(void)
{
    LOG_INFO("Testing wdt_start_reset");

    // Set timeout and start watchdog
    wdt_set_timeout(5);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Advance time but not enough to trigger timeout
    utest_systick_advance_time_ms(4000);
    wdt_do_periodic_work();

    // Should not have timed out yet
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not timeout before period expires");

    // Reset the watchdog
    wdt_start_reset();

    // Advance another 4 seconds - still should not timeout since we reset
    utest_systick_advance_time_ms(4000);
    wdt_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not timeout after reset");

    // Advance past timeout from the reset point
    utest_systick_advance_time_ms(2000);
    wdt_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "Watchdog should timeout after period from reset point");
}

// Scenario: Start watchdog with timeout, stop it, then wait past timeout
// Expected: Stopped watchdog does not trigger timeout
static void test_wdt_stop(void)
{
    LOG_INFO("Testing wdt_stop");

    // Set timeout and start watchdog
    wdt_set_timeout(2);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Stop the watchdog
    wdt_stop();

    // Advance time past timeout
    utest_systick_advance_time_ms(3000);
    wdt_do_periodic_work();

    // Should not have timed out because watchdog is stopped
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Stopped watchdog should not timeout");
}

// Scenario: Start watchdog and wait for timeout to be reached
// Expected: No timeout just before period, timeout triggered after period
static void test_wdt_timeout_triggers(void)
{
    LOG_INFO("Testing watchdog timeout triggers");

    // Set timeout and start watchdog
    wdt_set_timeout(3);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Advance time just before timeout
    utest_systick_advance_time_ms(2999);
    wdt_do_periodic_work();

    // Should not have timed out yet
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not timeout before timeout period");

    // Advance time to trigger timeout
    utest_systick_advance_time_ms(2);
    wdt_do_periodic_work();

    // Should have timed out
    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "Watchdog should timeout after timeout period");
}

// Scenario: Trigger watchdog timeout and call handler twice
// Expected: First call returns true and clears flag, second call returns false
static void test_wdt_handle_timed_out_clears_flag(void)
{
    LOG_INFO("Testing wdt_handle_timed_out clears flag");

    // Set timeout and start watchdog
    wdt_set_timeout(1);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Trigger timeout
    utest_systick_advance_time_ms(1100);
    wdt_do_periodic_work();

    // First call should return true
    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "First call should return true for timed out flag");

    // Second call should return false (flag cleared)
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Second call should return false - flag must be cleared");
}

// Scenario: Trigger watchdog timeout twice in sequence
// Expected: Watchdog auto-resets after first timeout and triggers timeout again
// after another period
static void test_wdt_timeout_auto_resets(void)
{
    LOG_INFO("Testing watchdog auto-resets after timeout");

    // Set timeout and start watchdog
    wdt_set_timeout(2);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Trigger first timeout
    utest_systick_advance_time_ms(2100);
    wdt_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "First timeout should be triggered");

    // Watchdog should auto-reset, so advance time again
    utest_systick_advance_time_ms(2100);
    wdt_do_periodic_work();

    // Should timeout again
    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "Second timeout should be triggered after auto-reset");
}

// Scenario: Change watchdog timeout via regmap from 10s to 5s
// Expected: Timeout updated in regmap, watchdog reset, new timeout period
// applies
static void test_wdt_regmap_timeout_change(void)
{
    LOG_INFO("Testing watchdog timeout change via regmap");

    // Start with initial timeout
    wdt_set_timeout(10);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Simulate timeout change from regmap
    struct REGMAP_WDT w = {
        .timeout = 5,
        .reset = 0
    };

    // Mark region as changed and write data
    TEST_ASSERT_TRUE_MESSAGE(regmap_set_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to set WDT regmap data");
    utest_regmap_mark_region_changed(REGMAP_REGION_WDT);

    // Process periodic work
    wdt_do_periodic_work();

    // Verify timeout was updated in regmap
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(5, w.timeout,
                                     "Timeout should be updated to 5 seconds from regmap");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, w.reset,
                                     "Reset flag should be cleared");

    // Verify watchdog was reset (timestamp should be updated)
    // Advance time less than new timeout
    utest_systick_advance_time_ms(4000);
    wdt_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not timeout before new timeout period");

    // Advance to trigger new timeout
    utest_systick_advance_time_ms(1100);
    wdt_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "Watchdog should timeout after new timeout period");
}

// Scenario: After 8s elapsed with 10s timeout, reduce timeout to 5s via regmap
// Expected: Watchdog auto-resets to prevent false trigger (8s > 5s but should
// not trigger); new 5s timeout applies from reset point
static void test_wdt_regmap_timeout_decrease_prevents_false_trigger(void)
{
    LOG_INFO("Testing watchdog auto-reset when timeout decreased via regmap");

    // Start with large timeout (10 seconds)
    wdt_set_timeout(10);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Advance time significantly (8 seconds) but still less than current timeout
    utest_systick_advance_time_ms(8000);
    wdt_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not timeout at 8s with 10s timeout");

    // Now change timeout to smaller value (5 seconds) via regmap
    // Without auto-reset, this would cause false trigger since 8s > 5s
    struct REGMAP_WDT w = {
        .timeout = 5,
        .reset = 0
    };

    TEST_ASSERT_TRUE_MESSAGE(regmap_set_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to set WDT regmap data");
    utest_regmap_mark_region_changed(REGMAP_REGION_WDT);

    // Process periodic work - should automatically reset watchdog
    wdt_do_periodic_work();

    // Verify timeout was updated
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(5, w.timeout,
                                     "Timeout should be updated to 5 seconds");

    // The critical check: watchdog should NOT have triggered yet
    // because it was auto-reset when timeout was changed
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not trigger - auto-reset prevents false trigger");

    // Now advance time less than new timeout period (4 seconds)
    utest_systick_advance_time_ms(4000);
    wdt_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not timeout before new timeout period");

    // Advance past new timeout period
    utest_systick_advance_time_ms(1100);
    wdt_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "Watchdog should timeout after new timeout period from reset point");
}

// Scenario: Send watchdog reset command via regmap after 4s of 5s timeout
// Expected: Reset flag cleared, watchdog timer resets, waits another 5s before
// timeout
static void test_wdt_regmap_reset_command(void)
{
    LOG_INFO("Testing watchdog reset command via regmap");

    // Set timeout and start
    wdt_set_timeout(5);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Advance time
    utest_systick_advance_time_ms(4000);
    wdt_do_periodic_work();

    // Send reset command via regmap
    struct REGMAP_WDT w = {
        .timeout = 5,
        .reset = 1
    };

    TEST_ASSERT_TRUE_MESSAGE(regmap_set_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to set WDT regmap data");
    utest_regmap_mark_region_changed(REGMAP_REGION_WDT);

    // Process periodic work (should reset watchdog)
    wdt_do_periodic_work();

    // Verify reset flag is cleared in regmap
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, w.reset,
                                     "Reset flag should be cleared after processing");

    // Advance time - should not timeout yet because it was reset
    utest_systick_advance_time_ms(4000);
    wdt_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not timeout - it was just reset via regmap");

    // Advance to trigger timeout from reset point
    utest_systick_advance_time_ms(1100);
    wdt_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "Watchdog should timeout after period from reset");
}

// Scenario: Change timeout from 10s to 3s AND set reset flag simultaneously
// Expected: Both operations applied, timeout updated to 3s, reset flag cleared,
// watchdog reset; new timeout period applies
static void test_wdt_regmap_timeout_and_reset_simultaneous(void)
{
    LOG_INFO("Testing simultaneous timeout change and reset flag via regmap");

    // Set timeout and start
    wdt_set_timeout(10);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Advance time significantly
    utest_systick_advance_time_ms(7000);
    wdt_do_periodic_work();

    // Now send both timeout change AND reset flag in one transaction
    struct REGMAP_WDT w = {
        .timeout = 3,
        .reset = 1
    };

    TEST_ASSERT_TRUE_MESSAGE(regmap_set_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to set WDT regmap data");
    utest_regmap_mark_region_changed(REGMAP_REGION_WDT);

    // Process periodic work (should change timeout and reset watchdog)
    wdt_do_periodic_work();

    // Verify timeout was updated and reset flag cleared
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3, w.timeout,
                                     "Timeout should be updated to 3 seconds");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, w.reset,
                                     "Reset flag should be cleared after processing");

    // Verify watchdog was reset - should not timeout immediately
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not timeout immediately after simultaneous changes");

    // Advance time less than new timeout
    utest_systick_advance_time_ms(2500);
    wdt_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not timeout before new timeout period");

    // Advance past new timeout period
    utest_systick_advance_time_ms(600);
    wdt_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "Watchdog should timeout after new timeout period");
}

// Scenario: Set timeout to 0 and over-max value via regmap
// Expected: Zero timeout clamped to 1s, over-max timeout clamped to
// WBEC_WATCHDOG_MAX_TIMEOUT_S
static void test_wdt_regmap_timeout_bounds_via_regmap(void)
{
    LOG_INFO("Testing watchdog timeout bounds via regmap");

    // Test zero timeout via regmap (should be clamped to 1)
    struct REGMAP_WDT w = {
        .timeout = 0,
        .reset = 0
    };

    TEST_ASSERT_TRUE_MESSAGE(regmap_set_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to set WDT regmap data");
    utest_regmap_mark_region_changed(REGMAP_REGION_WDT);
    wdt_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, w.timeout,
                                     "Zero timeout from regmap should be clamped to 1");

    // Test over-max timeout via regmap (should be clamped to max)
    w.timeout = WBEC_WATCHDOG_MAX_TIMEOUT_S + 50;
    w.reset = 0;

    TEST_ASSERT_TRUE_MESSAGE(regmap_set_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to set WDT regmap data");
    utest_regmap_mark_region_changed(REGMAP_REGION_WDT);
    wdt_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(WBEC_WATCHDOG_MAX_TIMEOUT_S, w.timeout,
                                     "Over-max timeout from regmap should be clamped to max");
}

// Scenario: Call periodic work without marking regmap region as changed
// Expected: Regmap values remain unchanged, reset flag stays 0
static void test_wdt_regmap_no_change(void)
{
    LOG_INFO("Testing watchdog when regmap has no changes");

    // Set initial timeout
    wdt_set_timeout(10);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Get current regmap state
    struct REGMAP_WDT w_before;
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w_before, sizeof(w_before)),
                             "Failed to get WDT regmap data");

    // Call periodic work without marking region as changed
    utest_systick_advance_time_ms(1000);
    wdt_do_periodic_work();

    // Regmap should remain unchanged
    struct REGMAP_WDT w_after;
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w_after, sizeof(w_after)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(w_before.timeout, w_after.timeout,
                                     "Timeout should remain unchanged when regmap not changed");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, w_after.reset,
                                     "Reset flag should be 0");
}

// Scenario: Reset watchdog 5 times before timeout period elapses
// Expected: Watchdog does not timeout when reset repeatedly; times out only
// when not reset
static void test_wdt_multiple_resets(void)
{
    LOG_INFO("Testing multiple watchdog resets");

    wdt_set_timeout(3);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Reset multiple times before timeout
    for (int i = 0; i < 5; i++) {
        utest_systick_advance_time_ms(2000);
        wdt_start_reset();
        wdt_do_periodic_work();
        TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                                  "Watchdog should not timeout when reset before period");
    }

    // Now let it timeout
    utest_systick_advance_time_ms(3100);
    wdt_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "Watchdog should timeout when not reset");
}

// Scenario: Set watchdog to maximum timeout and wait for it to trigger
// Expected: Watchdog does not timeout just before max timeout period;
// times out after max timeout period elapses
static void test_wdt_long_timeout(void)
{
    LOG_INFO("Testing watchdog with long timeout period");

    // Use maximum timeout
    wdt_set_timeout(WBEC_WATCHDOG_MAX_TIMEOUT_S);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Advance time to just before timeout
    utest_systick_advance_time_ms(WBEC_WATCHDOG_MAX_TIMEOUT_S * 1000 - 100);
    wdt_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not timeout before max timeout period");

    // Trigger timeout
    utest_systick_advance_time_ms(200);
    wdt_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "Watchdog should timeout after max timeout period");
}

int main(void)
{
    UNITY_BEGIN();

    // Basic functionality tests
    RUN_TEST(test_wdt_set_timeout_normal);
    RUN_TEST(test_wdt_set_timeout_zero);
    RUN_TEST(test_wdt_set_timeout_max);
    RUN_TEST(test_wdt_set_timeout_over_max);

    // Start/stop tests
    RUN_TEST(test_wdt_start_reset);
    RUN_TEST(test_wdt_stop);

    // Timeout behavior tests
    RUN_TEST(test_wdt_timeout_triggers);
    RUN_TEST(test_wdt_handle_timed_out_clears_flag);
    RUN_TEST(test_wdt_timeout_auto_resets);

    // Regmap integration tests
    RUN_TEST(test_wdt_regmap_timeout_change);
    RUN_TEST(test_wdt_regmap_timeout_decrease_prevents_false_trigger);
    RUN_TEST(test_wdt_regmap_reset_command);
    RUN_TEST(test_wdt_regmap_timeout_and_reset_simultaneous);
    RUN_TEST(test_wdt_regmap_timeout_bounds_via_regmap);
    RUN_TEST(test_wdt_regmap_no_change);

    // Edge case tests
    RUN_TEST(test_wdt_multiple_resets);
    RUN_TEST(test_wdt_long_timeout);

    return UNITY_END();
}
