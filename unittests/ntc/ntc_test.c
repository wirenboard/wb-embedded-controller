#include "unity.h"
#include "ntc.h"
#include "fix16.h"
#include "config.h"
#include <stdbool.h>
#include <stdio.h>

#define LOG_LEVEL LOG_LEVEL_INFO
#include "console_log.h"

#define ADC_MAX_VAL 4095

void setUp(void)
{
    // Nothing to initialize for NTC tests
}

void tearDown(void)
{
    // Nothing to clean up
}


static const char* get_ntc_table_monotonicity_test_message(fix16_t temp, fix16_t prev_temp, fix16_t resistance)
{
    static char msg[100] = {0};

    int res_int = fix16_to_int(resistance);
    int res_frac = (int)((resistance & 0xFFFF) * 1000 / 65536);

    snprintf(
        msg, sizeof(msg),
        "Temperature should increase as resistance decreases. "
        "At %d.%03dkΩ got %d°C, previous was %d°C",
        res_int, res_frac,
        fix16_to_int(temp),
        fix16_to_int(prev_temp)
    );

    return msg;
}


static void test_ntc_table_monotonicity(void)
{
    LOG_INFO("Testing NTC table monotonicity (resistance decreases with temperature)");

    // Test that as resistance decreases, temperature increases (monotonic relationship)
    // This indirectly verifies that the internal NTC table is properly ordered
    // Table values (10k NTC): 740.654k, 517.001k, 366.615k, ..., 10.000k, ..., 0.312k
    // Corresponding temps: -55°C, -50°C, -45°C, ..., 25°C, ..., 150°C

    // Sample resistance values from different parts of the table in decreasing order
    fix16_t resistances[] = {
        F16(700.0),   // Near -55°C
        F16(500.0),   // Near -50°C
        F16(300.0),   // Near -45°C
        F16(100.0),   // Near -30°C
        F16(50.0),    // Near -10°C
        F16(20.0),    // Near 5°C
        F16(10.0),    // Near 25°C
        F16(5.0),     // Near 50°C
        F16(2.0),     // Near 75°C
        F16(1.0),     // Near 95°C
        F16(0.5),     // Near 130°C
    };

    fix16_t prev_temp = F16(-100);  // Start with very low temperature

    for (size_t i = 0; i < sizeof(resistances) / sizeof(resistances[0]); i++) {
        fix16_t temp = ntc_kohm_to_temp(resistances[i]);

        TEST_ASSERT_GREATER_THAN_INT32_MESSAGE(
            prev_temp, temp,
            get_ntc_table_monotonicity_test_message(temp, prev_temp, resistances[i])
        );

        prev_temp = temp;
    }
}


static void test_ntc_kohm_to_temp_boundary_conditions(void)
{
    LOG_INFO("Testing ntc_kohm_to_temp boundary conditions");

    // Test extremely high resistance (above table max) - should return min temp (-55°C)
    fix16_t very_high_kohm = F16(1000.0);
    fix16_t temp = ntc_kohm_to_temp(very_high_kohm);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(-55, fix16_to_int(temp),
                                    "Very high resistance should return minimum temperature -55°C");

    // Test extremely low resistance (below table min) - should return max temp (150°C)
    fix16_t very_low_kohm = F16(0.1);
    temp = ntc_kohm_to_temp(very_low_kohm);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(150, fix16_to_int(temp),
                                    "Very low resistance should return maximum temperature 150°C");

    // Test zero resistance - should return max temp
    temp = ntc_kohm_to_temp(F16(0));
    TEST_ASSERT_EQUAL_INT16_MESSAGE(150, fix16_to_int(temp),
                                    "Zero resistance should return maximum temperature");

    // Test first table entry value (740.654k at -55°C)
    fix16_t first_entry = F16(740.654);
    temp = ntc_kohm_to_temp(first_entry);
    TEST_ASSERT_INT16_WITHIN_MESSAGE(1, -55, fix16_to_int(temp),
                                     "At first table entry 740.654kΩ, temperature should be -55°C");

    // Test last table entry value (0.312k at 150°C)
    fix16_t last_entry = F16(0.312);
    temp = ntc_kohm_to_temp(last_entry);
    TEST_ASSERT_INT16_WITHIN_MESSAGE(1, 150, fix16_to_int(temp),
                                     "At last table entry 0.312kΩ, temperature should be 150°C");
}


static void test_ntc_kohm_to_temp_25_degrees(void)
{
    LOG_INFO("Testing ntc_kohm_to_temp at ~25°C (nominal resistance)");

    // At 25°C, 10k NTC should have ~10k resistance
    // From the table, at 25°C (index 16 in the table), resistance is 10.000k
    fix16_t ntc_10k = F16(10.0);
    fix16_t temp = ntc_kohm_to_temp(ntc_10k);

    // Temperature should be around 25°C
    // The table spans from -55 to 150°C with 42 entries
    // Entry 16 corresponds to: -55 + 16 * (150 - (-55)) / (42 - 1) = -55 + 16 * 5 = 25°C
    TEST_ASSERT_INT16_WITHIN_MESSAGE(1, 25, fix16_to_int(temp),
                                     "At 10kΩ, temperature should be approximately 25°C");
}


static void test_ntc_kohm_to_temp_interpolation(void)
{
    LOG_INFO("Testing ntc_kohm_to_temp interpolation between table values");

    // Test interpolation between two known table values
    // From the 10k NTC table:
    // Entry 16: 10.000k -> 25°C
    // Entry 17: 8.240k -> 30°C

    // Test a value between these two
    fix16_t mid_kohm = F16(9.0); // Between 10.000 and 8.240
    fix16_t temp = ntc_kohm_to_temp(mid_kohm);

    // Temperature should be between 25 and 30°C
    int16_t temp_int = fix16_to_int(temp);
    TEST_ASSERT_TRUE_MESSAGE(temp_int > 25 && temp_int < 30,
                            "Interpolated temperature should be between 25°C and 30°C");
}


static void test_ntc_kohm_to_temp_interpolation_negative(void)
{
    LOG_INFO("Testing ntc_kohm_to_temp interpolation at negative temperatures");

    // Test interpolation between two known table values in negative range
    // From the 10k NTC table:
    // Entry 9: 47.652k -> -10°C
    // Entry 10: 37.186k -> -5°C

    // Test a value between these two
    fix16_t mid_kohm = F16(42.0); // Between 47.652 and 37.186
    fix16_t temp = ntc_kohm_to_temp(mid_kohm);

    // Temperature should be between -10 and -5°C
    int16_t temp_int = fix16_to_int(temp);
    TEST_ASSERT_TRUE_MESSAGE(temp_int > -10 && temp_int < -5,
                            "Interpolated temperature should be between -10°C and -5°C");
}


static void test_ntc_kohm_to_temp_cold(void)
{
    LOG_INFO("Testing ntc_kohm_to_temp at cold temperatures");

    // Test at 0°C (entry 11 in table)
    // Entry 11: 29.283k -> 0°C
    fix16_t kohm_0c = F16(29.283);
    fix16_t temp = ntc_kohm_to_temp(kohm_0c);

    // Should be around 0°C
    TEST_ASSERT_INT16_WITHIN_MESSAGE(2, 0, fix16_to_int(temp),
                                     "At 29.283kΩ, temperature should be approximately 0°C");
}


static void test_ntc_kohm_to_temp_hot(void)
{
    LOG_INFO("Testing ntc_kohm_to_temp at high temperatures");

    // Test at 100°C (entry 31 in table)
    // Entry 31: 0.945k -> 100°C
    fix16_t kohm_100c = F16(0.945);
    fix16_t temp = ntc_kohm_to_temp(kohm_100c);

    // Should be around 100°C
    TEST_ASSERT_INT16_WITHIN_MESSAGE(2, 100, fix16_to_int(temp),
                                     "At 0.945kΩ, temperature should be approximately 100°C");
}


static void test_ntc_convert_adc_raw_to_temp_mid_range(void)
{
    LOG_INFO("Testing ntc_convert_adc_raw_to_temp with mid-range ADC value");

    // For a voltage divider with Rntc and Rpullup:
    // Vout = Vin * Rntc / (Rntc + Rpullup)
    // ADC = Vout / Vin * ADC_MAX
    // ADC = Rntc / (Rntc + Rpullup) * ADC_MAX

    // For Rntc = 10kΩ and Rpullup = 33kΩ:
    // ADC = 10 / (10 + 33) * 4095 = 10 / 43 * 4095 ≈ 952
    fix16_t adc_val = fix16_from_int(952);
    fix16_t temp = ntc_convert_adc_raw_to_temp(adc_val);

    // This should give approximately 25°C
    TEST_ASSERT_INT16_WITHIN_MESSAGE(5, 25, fix16_to_int(temp),
                                     "ADC value 952 should correspond to approximately 25°C");
}


static void test_ntc_convert_adc_raw_to_temp_cold(void)
{
    LOG_INFO("Testing ntc_convert_adc_raw_to_temp at cold temperature (high resistance)");

    // For cold temperature, NTC resistance is high
    // At 0°C, Rntc ≈ 29.283kΩ
    // ADC = 29.283 / (29.283 + 33) * 4095 ≈ 1926
    fix16_t adc_val = fix16_from_int(1926);
    fix16_t temp = ntc_convert_adc_raw_to_temp(adc_val);

    // This should give approximately 0°C
    TEST_ASSERT_INT16_WITHIN_MESSAGE(5, 0, fix16_to_int(temp),
                                     "ADC value 1926 should correspond to approximately 0°C");
}


static void test_ntc_convert_adc_raw_to_temp_hot(void)
{
    LOG_INFO("Testing ntc_convert_adc_raw_to_temp at high temperature (low resistance)");

    // For hot temperature, NTC resistance is low
    // At 100°C, Rntc ≈ 0.945kΩ
    // ADC = 0.945 / (0.945 + 33) * 4095 ≈ 114
    fix16_t adc_val = fix16_from_int(114);
    fix16_t temp = ntc_convert_adc_raw_to_temp(adc_val);

    // This should give approximately 100°C
    TEST_ASSERT_INT16_WITHIN_MESSAGE(5, 100, fix16_to_int(temp),
                                     "ADC value 114 should correspond to approximately 100°C");
}


static void test_ntc_convert_adc_raw_to_temp_adc_zero(void)
{
    LOG_INFO("Testing ntc_convert_adc_raw_to_temp with ADC = 0 (short circuit)");

    // ADC = 0 means Rntc = 0 (short circuit)
    fix16_t adc_val = F16(0);
    fix16_t temp = ntc_convert_adc_raw_to_temp(adc_val);

    // Should return maximum temperature
    TEST_ASSERT_EQUAL_INT16_MESSAGE(150, fix16_to_int(temp),
                                    "ADC = 0 should return maximum temperature 150°C");
}


static void test_ntc_convert_adc_raw_to_temp_adc_max(void)
{
    LOG_INFO("Testing ntc_convert_adc_raw_to_temp with ADC = MAX (open circuit)");

    // ADC = ADC_MAX means Rntc = infinity (open circuit)
    // The function should handle this gracefully by returning 0 resistance
    fix16_t adc_val = fix16_from_int(ADC_MAX_VAL);
    fix16_t temp = ntc_convert_adc_raw_to_temp(adc_val);

    // According to the code, when adc_val >= adc_max_val, resistance is 0
    // So it should return maximum temperature
    TEST_ASSERT_EQUAL_INT16_MESSAGE(150, fix16_to_int(temp),
                                    "ADC = MAX should return maximum temperature 150°C");
}


typedef struct {
    int adc_value;
    int expected_temp_min;
    int expected_temp_max;
} test_case_t;

static const char* get_ntc_convert_adc_raw_to_temp_various_values_test_msg(
    const test_case_t* test_case,
    int temp
)
{
    static char msg[100] = {0};

    snprintf(
        msg, sizeof(msg),
        "ADC=%d should produce temp between %d and %d°C, got %d°C",
        test_case->adc_value,
        test_case->expected_temp_min,
        test_case->expected_temp_max,
        temp
    );

    return msg;
}

static void test_ntc_convert_adc_raw_to_temp_various_values(void)
{
    LOG_INFO("Testing ntc_convert_adc_raw_to_temp with various ADC values");

    // Test a range of ADC values to ensure they produce reasonable temperatures
    test_case_t test_cases[] = {
        {100,  80,  150},  // Very low ADC -> hot
        {500,  40,  55},   // Low-mid ADC -> warm
        {1000, 15,  35},   // Mid ADC -> room temp
        {2000, -10, 10},   // High-mid ADC -> cool
        {3000, -40, -10},  // High ADC -> cold
    };

    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        fix16_t adc_val = fix16_from_int(test_cases[i].adc_value);
        fix16_t temp = ntc_convert_adc_raw_to_temp(adc_val);
        int16_t temp_int = fix16_to_int(temp);

        TEST_ASSERT_TRUE_MESSAGE(
            temp_int >= test_cases[i].expected_temp_min &&
            temp_int <= test_cases[i].expected_temp_max,
            get_ntc_convert_adc_raw_to_temp_various_values_test_msg(&test_cases[i], temp_int)
        );
    }
}


int main(void)
{
    UNITY_BEGIN();

    // Table sanity checks
    RUN_TEST(test_ntc_table_monotonicity);

    // Boundary conditions tests
    RUN_TEST(test_ntc_kohm_to_temp_boundary_conditions);

    // Temperature conversion tests at specific points
    RUN_TEST(test_ntc_kohm_to_temp_25_degrees);
    RUN_TEST(test_ntc_kohm_to_temp_cold);
    RUN_TEST(test_ntc_kohm_to_temp_hot);
    RUN_TEST(test_ntc_kohm_to_temp_interpolation);
    RUN_TEST(test_ntc_kohm_to_temp_interpolation_negative);

    // ADC to temperature conversion tests
    RUN_TEST(test_ntc_convert_adc_raw_to_temp_mid_range);
    RUN_TEST(test_ntc_convert_adc_raw_to_temp_cold);
    RUN_TEST(test_ntc_convert_adc_raw_to_temp_hot);
    RUN_TEST(test_ntc_convert_adc_raw_to_temp_adc_zero);
    RUN_TEST(test_ntc_convert_adc_raw_to_temp_adc_max);
    RUN_TEST(test_ntc_convert_adc_raw_to_temp_various_values);

    return UNITY_END();
}
