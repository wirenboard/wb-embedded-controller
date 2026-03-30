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

}

void tearDown(void)
{

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

// Сценарий: Проверка монотонной зависимости в таблице NTC (R уменьшается, T увеличивается)
// Ожидается: по мере уменьшения значений сопротивления, значения температуры увеличиваются монотонно
static void test_ntc_table_monotonicity(void)
{
    LOG_INFO("Testing NTC table monotonicity (resistance decreases with temperature)");

    // Проверка, что по мере уменьшения сопротивления температура увеличивается (монотонная зависимость)
    // Это косвенно проверяет, что внутренняя таблица NTC правильно упорядочена
    // Значения таблицы (10k NTC): 740.654k, 517.001k, 366.615k, ..., 10.000k, ..., 0.312k
    // Соответствующие температуры: -55°C, -50°C, -45°C, ..., 25°C, ..., 150°C

    // Значения сопротивления из разных частей таблицы в порядке убывания
    fix16_t resistances[] = {
        F16(700.0),   // Около -55°C
        F16(500.0),   // Около -50°C
        F16(300.0),   // Около -45°C
        F16(100.0),   // Около -30°C
        F16(50.0),    // Около -10°C
        F16(20.0),    // Около 5°C
        F16(10.0),    // Около 25°C
        F16(5.0),     // Около 50°C
        F16(2.0),     // Около 75°C
        F16(1.0),     // Около 95°C
        F16(0.5),     // Около 130°C
    };

    fix16_t prev_temp = F16(-100);  // Начинаем с очень низкой температуры

    for (size_t i = 0; i < sizeof(resistances) / sizeof(resistances[0]); i++) {
        fix16_t temp = ntc_kohm_to_temp(resistances[i]);

        TEST_ASSERT_GREATER_THAN_INT32_MESSAGE(
            prev_temp, temp,
            get_ntc_table_monotonicity_test_message(temp, prev_temp, resistances[i])
        );

        prev_temp = temp;
    }
}

// Сценарий: Преобразование сопротивления в температуру при граничных условиях
// Ожидается: очень высокое R возвращает -55°C (min), очень низкое/нулевое R возвращает 150°C (max),
// первые/последние записи таблицы соответствуют ожидаемым температурам
static void test_ntc_kohm_to_temp_boundary_conditions(void)
{
    LOG_INFO("Testing ntc_kohm_to_temp boundary conditions");

    // Проверка очень высокого сопротивления (выше максимума таблицы) - должно вернуть минимальную температуру (-55°C)
    fix16_t very_high_kohm = F16(1000.0);
    fix16_t temp = ntc_kohm_to_temp(very_high_kohm);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(-55, fix16_to_int(temp),
                                    "Very high resistance should return minimum temperature -55°C");

    // Проверка очень низкого сопротивления (ниже минимума таблицы) - должно вернуть максимальную температуру (150°C)
    fix16_t very_low_kohm = F16(0.1);
    temp = ntc_kohm_to_temp(very_low_kohm);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(150, fix16_to_int(temp),
                                    "Very low resistance should return maximum temperature 150°C");

    // Проверка нулевого сопротивления - должно вернуть максимальную температуру
    temp = ntc_kohm_to_temp(F16(0));
    TEST_ASSERT_EQUAL_INT16_MESSAGE(150, fix16_to_int(temp),
                                    "Zero resistance should return maximum temperature");

    // Проверка значения первой записи таблицы (740.654k при -55°C)
    fix16_t first_entry = F16(740.654);
    temp = ntc_kohm_to_temp(first_entry);
    TEST_ASSERT_INT16_WITHIN_MESSAGE(1, -55, fix16_to_int(temp),
                                     "At first table entry 740.654kΩ, temperature should be -55°C");

    // Проверка значения последней записи таблицы (0.312k при 150°C)
    fix16_t last_entry = F16(0.312);
    temp = ntc_kohm_to_temp(last_entry);
    TEST_ASSERT_INT16_WITHIN_MESSAGE(1, 150, fix16_to_int(temp),
                                     "At last table entry 0.312kΩ, temperature should be 150°C");
}

// Сценарий: Преобразование сопротивления 10кОм (номинальное значение NTC) в температуру
// Ожидается: температура приблизительно 25°C
static void test_ntc_kohm_to_temp_25_degrees(void)
{
    LOG_INFO("Testing ntc_kohm_to_temp at ~25°C (nominal resistance)");

    // При 25°C, 10k NTC должен иметь сопротивление ~10k
    // Из таблицы, при 25°C (индекс 16), сопротивление 10.000k
    fix16_t ntc_10k = F16(10.0);
    fix16_t temp = ntc_kohm_to_temp(ntc_10k);

    // Температура должна быть около 25°C
    // Таблица охватывает от -55 до 150°C с 42 записями
    // Запись 16 соответствует: -55 + 16 * (150 - (-55)) / (42 - 1) = -55 + 16 * 5 = 25°C
    TEST_ASSERT_INT16_WITHIN_MESSAGE(1, 25, fix16_to_int(temp),
                                     "At 10kΩ, temperature should be approximately 25°C");
}

// Сценарий: Преобразование сопротивления между двумя записями таблицы в температуру
// Ожидается: температура интерполирована между 25°C и 30°C
static void test_ntc_kohm_to_temp_interpolation(void)
{
    LOG_INFO("Testing ntc_kohm_to_temp interpolation between table values");

    // Проверка интерполяции между двумя известными значениями таблицы
    // Из таблицы 10k NTC:
    // Запись 16: 10.000k -> 25°C
    // Запись 17: 8.240k -> 30°C

    // Проверка значения температуры между этими двумя сопротивлениями
    fix16_t mid_kohm = F16(9.0); // Между 10.000 и 8.240
    fix16_t temp = ntc_kohm_to_temp(mid_kohm);

    // Температура должна быть между 25 и 30°C
    int16_t temp_int = fix16_to_int(temp);
    TEST_ASSERT_TRUE_MESSAGE(temp_int > 25 && temp_int < 30,
                            "Interpolated temperature should be between 25°C and 30°C");
}

// Сценарий: Преобразование сопротивления между двумя записями таблицы при отрицательных температурах
// Ожидается: температура интерполирована между -10°C и -5°C
static void test_ntc_kohm_to_temp_interpolation_negative(void)
{
    LOG_INFO("Testing ntc_kohm_to_temp interpolation at negative temperatures");

    // Проверка интерполяции между двумя известными значениями таблицы в отрицательном диапазоне
    // Из таблицы 10k NTC:
    // Запись 9: 47.652k -> -10°C
    // Запись 10: 37.186k -> -5°C

    // Проверка значения температуры между этими двумя сопротивлениями
    fix16_t mid_kohm = F16(42.0); // Между 47.652 и 37.186
    fix16_t temp = ntc_kohm_to_temp(mid_kohm);

    // Температура должна быть между -10 и -5°C
    int16_t temp_int = fix16_to_int(temp);
    TEST_ASSERT_TRUE_MESSAGE(temp_int > -10 && temp_int < -5,
                            "Interpolated temperature should be between -10°C and -5°C");
}

// Сценарий: Преобразование сопротивления при холодной температуре (0°C)
// Ожидается: Температура приблизительно 0°C
static void test_ntc_kohm_to_temp_cold(void)
{
    LOG_INFO("Testing ntc_kohm_to_temp at cold temperatures");

    // Тест при 0°C (запись 11 в таблице)
    // Запись 11: 29.283k -> 0°C
    fix16_t kohm_0c = F16(29.283);
    fix16_t temp = ntc_kohm_to_temp(kohm_0c);

    // Должно быть около 0°C
    TEST_ASSERT_INT16_WITHIN_MESSAGE(2, 0, fix16_to_int(temp),
                                     "At 29.283kΩ, temperature should be approximately 0°C");
}

// Сценарий: Преобразование сопротивления при высокой температуре (100°C)
// Ожидается: Температура приблизительно 100°C
static void test_ntc_kohm_to_temp_hot(void)
{
    LOG_INFO("Testing ntc_kohm_to_temp at high temperatures");

    // Тест при 100°C (запись 31 в таблице)
    // Запись 31: 0.945k -> 100°C
    fix16_t kohm_100c = F16(0.945);
    fix16_t temp = ntc_kohm_to_temp(kohm_100c);

    // Должно быть около 100°C
    TEST_ASSERT_INT16_WITHIN_MESSAGE(2, 100, fix16_to_int(temp),
                                     "At 0.945kΩ, temperature should be approximately 100°C");
}

// Сценарий: Преобразование значения ADC среднего диапазона в температуру (делитель напряжения с 33k)
// Ожидается: Значение ADC 952 соответствует приблизительно 25°C
static void test_ntc_convert_adc_raw_to_temp_mid_range(void)
{
    LOG_INFO("Testing ntc_convert_adc_raw_to_temp with mid-range ADC value");

    // Для делителя напряжения с Rntc и Rpullup:
    // Vout = Vin * Rntc / (Rntc + Rpullup)
    // ADC = Vout / Vin * ADC_MAX
    // ADC = Rntc / (Rntc + Rpullup) * ADC_MAX

    // Для Rntc = 10kΩ и Rpullup = 33kΩ:
    // ADC = 10 / (10 + 33) * 4095 = 10 / 43 * 4095 ≈ 952
    fix16_t adc_val = fix16_from_int(952);
    fix16_t temp = ntc_convert_adc_raw_to_temp(adc_val);

    // Это должно дать приблизительно 25°C
    TEST_ASSERT_INT16_WITHIN_MESSAGE(5, 25, fix16_to_int(temp),
                                     "ADC value 952 should correspond to approximately 25°C");
}

// Сценарий: Преобразование значения ADC, соответствующего высокому сопротивлению NTC (холодно)
// Ожидается: Значение ADC 1926 соответствует приблизительно 0°C
static void test_ntc_convert_adc_raw_to_temp_cold(void)
{
    LOG_INFO("Testing ntc_convert_adc_raw_to_temp at cold temperature (high resistance)");

    // При холодной температуре сопротивление NTC высокое
    // При 0°C, Rntc ≈ 29.283kΩ
    // ADC = 29.283 / (29.283 + 33) * 4095 ≈ 1926
    fix16_t adc_val = fix16_from_int(1926);
    fix16_t temp = ntc_convert_adc_raw_to_temp(adc_val);

    // Это должно дать приблизительно 0°C
    TEST_ASSERT_INT16_WITHIN_MESSAGE(5, 0, fix16_to_int(temp),
                                     "ADC value 1926 should correspond to approximately 0°C");
}

// Сценарий: Преобразование значения ADC, соответствующего низкому сопротивлению NTC (горячо)
// Ожидается: Значение ADC 114 соответствует приблизительно 100°C
static void test_ntc_convert_adc_raw_to_temp_hot(void)
{
    LOG_INFO("Testing ntc_convert_adc_raw_to_temp at high temperature (low resistance)");

    // При высокой температуре сопротивление NTC низкое
    // При 100°C, Rntc ≈ 0.945kΩ
    // ADC = 0.945 / (0.945 + 33) * 4095 ≈ 114
    fix16_t adc_val = fix16_from_int(114);
    fix16_t temp = ntc_convert_adc_raw_to_temp(adc_val);

    // Это должно дать приблизительно 100°C
    TEST_ASSERT_INT16_WITHIN_MESSAGE(5, 100, fix16_to_int(temp),
                                     "ADC value 114 should correspond to approximately 100°C");
}

// Сценарий: Преобразование значения ADC = 0 (короткое замыкание)
// Ожидается: Возвращает максимальную температуру 150°C
static void test_ntc_convert_adc_raw_to_temp_adc_zero(void)
{
    LOG_INFO("Testing ntc_convert_adc_raw_to_temp with ADC = 0 (short circuit)");

    // ADC = 0 означает Rntc = 0 (короткое замыкание)
    fix16_t adc_val = F16(0);
    fix16_t temp = ntc_convert_adc_raw_to_temp(adc_val);

    // Должно вернуть максимальную температуру
    TEST_ASSERT_EQUAL_INT16_MESSAGE(150, fix16_to_int(temp),
                                    "ADC = 0 should return maximum temperature 150°C");
}

// Сценарий: Преобразование значения ADC при максимуме (обрыв цепи)
// Ожидается: Возвращает максимальную температуру 150°C (обрабатывается как нулевое сопротивление)
static void test_ntc_convert_adc_raw_to_temp_adc_max(void)
{
    LOG_INFO("Testing ntc_convert_adc_raw_to_temp with ADC = MAX (open circuit)");

    // ADC = ADC_MAX означает Rntc = бесконечность (обрыв цепи)
    // Функция должна обработать это, возвращая 0 сопротивление
    fix16_t adc_val = fix16_from_int(ADC_MAX_VAL);
    fix16_t temp = ntc_convert_adc_raw_to_temp(adc_val);

    // Согласно коду, когда adc_val >= adc_max_val, сопротивление = 0
    // Поэтому должно вернуть максимальную температуру
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

// Сценарий: Преобразование различных значений ADC во всём диапазоне
// Ожидается: Все температуры попадают в ожидаемые диапазоны для своих значений ADC
static void test_ntc_convert_adc_raw_to_temp_various_values(void)
{
    LOG_INFO("Testing ntc_convert_adc_raw_to_temp with various ADC values");

    // Тест диапазона значений ADC для проверки, что они дают разумные температуры
    test_case_t test_cases[] = {
        {100,  80,  150},  // Очень низкое значение ADC -> горячо
        {500,  40,  55},   // Низкое значение ADC -> тепло
        {1000, 15,  35},   // Среднее значение ADC -> комнатная температура
        {2000, -10, 10},   // Высокое значение ADC -> прохладно
        {3000, -40, -10},  // Очень высокое значение ADC -> холодно
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

    // Проверки корректности таблицы
    RUN_TEST(test_ntc_table_monotonicity);

    // Тесты граничных условий
    RUN_TEST(test_ntc_kohm_to_temp_boundary_conditions);

    // Тесты преобразования температуры в конкретных точках
    RUN_TEST(test_ntc_kohm_to_temp_25_degrees);
    RUN_TEST(test_ntc_kohm_to_temp_cold);
    RUN_TEST(test_ntc_kohm_to_temp_hot);
    RUN_TEST(test_ntc_kohm_to_temp_interpolation);
    RUN_TEST(test_ntc_kohm_to_temp_interpolation_negative);

    // Тесты преобразования значения ADC в температуру
    RUN_TEST(test_ntc_convert_adc_raw_to_temp_mid_range);
    RUN_TEST(test_ntc_convert_adc_raw_to_temp_cold);
    RUN_TEST(test_ntc_convert_adc_raw_to_temp_hot);
    RUN_TEST(test_ntc_convert_adc_raw_to_temp_adc_zero);
    RUN_TEST(test_ntc_convert_adc_raw_to_temp_adc_max);
    RUN_TEST(test_ntc_convert_adc_raw_to_temp_various_values);

    return UNITY_END();
}
