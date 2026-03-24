#include "unity.h"
#include "config.h"
#include "wbmz-subsystem.h"
#include "regmap-int.h"
#include "regmap-structs.h"

// Test helpers
#include "utest_adc.h"
#include "utest_systick.h"
#include "utest_regmap.h"
#include "utest_wbmz_common.h"

#if defined(WBEC_WBMZ6_SUPPORT)
#include "utest_wbmz6_battery.h"
#endif

#define LOG_LEVEL LOG_LEVEL_INFO
#include "console_log.h"

#if defined(WBEC_WBMZ6_SUPPORT)
// Номинальное напряжение для тестов (середина диапазона)
#define TEST_NOMINAL_VOLTAGE_MV \
    ((WBEC_WBMZ6_SUPERCAP_VOLTAGE_MIN_MV + WBEC_WBMZ6_SUPERCAP_VOLTAGE_MAX_MV) / 2)

// Функция для сброса внутреннего состояния wbmz-subsystem
void utest_wbmz_subsystem_reset_state(void);
#endif

void setUp(void)
{
    // Сброс всех моков
    #if defined(WBEC_WBMZ6_SUPPORT)
        utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, 0);
        utest_systick_set_time_ms(WBEC_WBMZ6_POLL_PERIOD_MS);  // Установить время > 0 для срабатывания первого опроса
        utest_wbmz6_battery_reset();
        // Сброс внутреннего состояния wbmz-subsystem
        utest_wbmz_subsystem_reset_state();
    #else
        utest_systick_set_time_ms(0);
    #endif
    utest_regmap_reset();
    utest_wbmz_common_reset();

    // Установка дефолтных значений wbmz-common
    utest_wbmz_set_powered_from_wbmz(false);
    utest_set_wbmz_stepup_enabled(false);
    utest_wbmz_set_vbat_ok(true);
}

void tearDown(void)
{
    // Дополнительная очистка при необходимости
}

#if defined(WBEC_WBMZ6_SUPPORT)

// ============================================================================
// Тесты wbmz6-supercap: Определение наличия
// ============================================================================

// Сценарий: ADC возвращает напряжение выше порога обнаружения
// Ожидается: Subsystem обнаруживает supercap через regmap
static void test_supercap_is_present_when_voltage_above_threshold(void)
{
    LOG_INFO("Testing supercap detection: voltage above threshold");

    utest_wbmz6_battery_set_present(false);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, WBEC_WBMZ6_SUPERCAP_DETECT_VOLTAGE_MV + 1);

    wbmz_subsystem_do_periodic_work();

    struct REGMAP_PWR_STATUS pwr_status = {};
    utest_regmap_get_region_data(REGMAP_REGION_PWR_STATUS, &pwr_status, sizeof(pwr_status));

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        1, pwr_status.wbmz_supercap_present,
        "Supercap should be detected when voltage is above threshold"
    );
}

// Сценарий: ADC возвращает напряжение ниже порога обнаружения
// Ожидается: Subsystem не обнаруживает supercap через regmap
static void test_supercap_not_present_when_voltage_below_threshold(void)
{
    LOG_INFO("Testing supercap detection: voltage below threshold");

    utest_wbmz6_battery_set_present(false);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, WBEC_WBMZ6_SUPERCAP_DETECT_VOLTAGE_MV - 1);

    wbmz_subsystem_do_periodic_work();

    struct REGMAP_PWR_STATUS pwr_status = {};
    utest_regmap_get_region_data(REGMAP_REGION_PWR_STATUS, &pwr_status, sizeof(pwr_status));

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, pwr_status.wbmz_supercap_present,
        "Supercap should NOT be detected when voltage is below threshold"
    );
}

// ============================================================================
// Тесты wbmz6-supercap: Инициализация
// ============================================================================

// Сценарий: Subsystem инициализирует supercap
// Ожидается: Параметры корректно записаны в regmap
static void test_supercap_init_sets_correct_params(void)
{
    LOG_INFO("Testing supercap initialization");

    utest_wbmz6_battery_set_present(false);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, TEST_NOMINAL_VOLTAGE_MV);

    wbmz_subsystem_do_periodic_work();

    struct REGMAP_PWR_STATUS pwr_status = {};
    utest_regmap_get_region_data(REGMAP_REGION_PWR_STATUS, &pwr_status, sizeof(pwr_status));

    TEST_ASSERT_GREATER_THAN_UINT16_MESSAGE(
        0, pwr_status.wbmz_full_design_capacity,
        "Design capacity should be set"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        WBEC_WBMZ6_SUPERCAP_VOLTAGE_MIN_MV, pwr_status.wbmz_voltage_min_design,
        "Min voltage should match config"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        WBEC_WBMZ6_SUPERCAP_VOLTAGE_MAX_MV, pwr_status.wbmz_voltage_max_design,
        "Max voltage should match config"
    );
    TEST_ASSERT_GREATER_THAN_UINT16_MESSAGE(
        0, pwr_status.wbmz_constant_charge_current,
        "Charge current should be set"
    );
}

// ============================================================================
// Тесты wbmz6-supercap: Обновление статуса
// ============================================================================

// Сценарий: Напряжение на максимуме
// Ожидается: capacity_percent = 100%, is_dead = false через regmap
static void test_supercap_status_at_max_voltage(void)
{
    LOG_INFO("Testing supercap status at max voltage");

    utest_wbmz6_battery_set_present(false);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, WBEC_WBMZ6_SUPERCAP_VOLTAGE_MAX_MV);

    wbmz_subsystem_do_periodic_work();

    struct REGMAP_PWR_STATUS pwr_status = {};
    utest_regmap_get_region_data(REGMAP_REGION_PWR_STATUS, &pwr_status, sizeof(pwr_status));

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        WBEC_WBMZ6_SUPERCAP_VOLTAGE_MAX_MV, pwr_status.wbmz_battery_voltage,
        "Voltage should match ADC reading"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        100, pwr_status.wbmz_capacity_percent,
        "Capacity should be 100% at max voltage"
    );
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, pwr_status.wbmz_is_dead,
        "Supercap should not be dead at max voltage"
    );
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        1, pwr_status.wbmz_is_inserted,
        "Supercap should always report as inserted"
    );
}

// Сценарий: Напряжение на минимуме
// Ожидается: capacity_percent = 0% через regmap
static void test_supercap_status_at_min_voltage(void)
{
    LOG_INFO("Testing supercap status at min voltage");

    utest_wbmz6_battery_set_present(false);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, WBEC_WBMZ6_SUPERCAP_VOLTAGE_MIN_MV);

    wbmz_subsystem_do_periodic_work();

    struct REGMAP_PWR_STATUS pwr_status = {};
    utest_regmap_get_region_data(REGMAP_REGION_PWR_STATUS, &pwr_status, sizeof(pwr_status));

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, pwr_status.wbmz_capacity_percent,
        "Capacity should be 0% at min voltage"
    );
}

// Сценарий: Напряжение ниже минимума
// Ожидается: is_dead = true через regmap
static void test_supercap_dead_when_voltage_below_min(void)
{
    LOG_INFO("Testing supercap dead flag below min voltage");

    utest_wbmz6_battery_set_present(false);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, WBEC_WBMZ6_SUPERCAP_VOLTAGE_MIN_MV - 100);

    wbmz_subsystem_do_periodic_work();

    struct REGMAP_PWR_STATUS pwr_status = {};
    utest_regmap_get_region_data(REGMAP_REGION_PWR_STATUS, &pwr_status, sizeof(pwr_status));

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        1, pwr_status.wbmz_is_dead,
        "Supercap should be marked as dead when voltage below minimum"
    );
}

// Сценарий: Напряжение выше максимума
// Ожидается: capacity_percent = 100% (ограничен сверху) через regmap
static void test_supercap_capacity_capped_when_voltage_above_max(void)
{
    LOG_INFO("Testing supercap capacity capped at 100%% when voltage above max");

    utest_wbmz6_battery_set_present(false);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, WBEC_WBMZ6_SUPERCAP_VOLTAGE_MAX_MV + 100);

    wbmz_subsystem_do_periodic_work();

    struct REGMAP_PWR_STATUS pwr_status = {};
    utest_regmap_get_region_data(REGMAP_REGION_PWR_STATUS, &pwr_status, sizeof(pwr_status));

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        100, pwr_status.wbmz_capacity_percent,
        "Capacity should be capped at 100% when voltage above max"
    );
}

// Сценарий: Промежуточные значения напряжения
// Ожидается: Корректный расчет capacity по формуле энергии (W = 1/2 × C × V²)
static void test_supercap_capacity_calculated_correctly_at_intermediate_voltages(void)
{
    LOG_INFO("Testing supercap energy calculation at intermediate voltages");

    utest_wbmz6_battery_set_present(false);
    struct REGMAP_PWR_STATUS pwr_status = {};

    // Вычисление ожидаемой емкости по формуле: capacity = (V² - V_min²) / (V_max² - V_min²) * 100
    const uint32_t v_min = WBEC_WBMZ6_SUPERCAP_VOLTAGE_MIN_MV;
    const uint32_t v_max = WBEC_WBMZ6_SUPERCAP_VOLTAGE_MAX_MV;
    const uint32_t v_range = v_max - v_min;

    // Тест 1: V = V_min + 25% диапазона (ближе к минимуму)
    const uint16_t test_voltage_1 = v_min + v_range / 4;
    const uint32_t energy_range = v_max * v_max - v_min * v_min;
    const uint32_t energy_1 = test_voltage_1 * test_voltage_1 - v_min * v_min;
    const uint16_t expected_capacity_1 = (energy_1 * 100 + energy_range / 2) / energy_range;

    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, test_voltage_1);
    wbmz_subsystem_do_periodic_work();
    utest_regmap_get_region_data(REGMAP_REGION_PWR_STATUS, &pwr_status, sizeof(pwr_status));

    TEST_ASSERT_UINT16_WITHIN_MESSAGE(
        2, expected_capacity_1, pwr_status.wbmz_capacity_percent,
        "Capacity should match energy formula at V_min + 25%% of range"
    );

    // Сброс состояния для следующего теста
    utest_wbmz_subsystem_reset_state();
    utest_systick_advance_time_ms(WBEC_WBMZ6_POLL_PERIOD_MS);

    // Тест 2: V = (V_min + V_max) / 2 (середина диапазона по напряжению)
    const uint16_t test_voltage_2 = (v_min + v_max) / 2;
    const uint32_t energy_2 = test_voltage_2 * test_voltage_2 - v_min * v_min;
    const uint16_t expected_capacity_2 = (energy_2 * 100 + energy_range / 2) / energy_range;

    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, test_voltage_2);
    wbmz_subsystem_do_periodic_work();
    utest_regmap_get_region_data(REGMAP_REGION_PWR_STATUS, &pwr_status, sizeof(pwr_status));

    TEST_ASSERT_UINT16_WITHIN_MESSAGE(
        2, expected_capacity_2, pwr_status.wbmz_capacity_percent,
        "Capacity < 50%% at voltage midpoint (quadratic energy dependence)"
    );

    // Сброс состояния для следующего теста
    utest_wbmz_subsystem_reset_state();
    utest_systick_advance_time_ms(WBEC_WBMZ6_POLL_PERIOD_MS);

    // Тест 3: V = V_min + 75% диапазона (ближе к максимуму)
    const uint16_t test_voltage_3 = v_min + (v_range * 3) / 4;
    const uint32_t energy_3 = test_voltage_3 * test_voltage_3 - v_min * v_min;
    const uint16_t expected_capacity_3 = (energy_3 * 100 + energy_range / 2) / energy_range;

    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, test_voltage_3);
    wbmz_subsystem_do_periodic_work();
    utest_regmap_get_region_data(REGMAP_REGION_PWR_STATUS, &pwr_status, sizeof(pwr_status));

    TEST_ASSERT_UINT16_WITHIN_MESSAGE(
        2, expected_capacity_3, pwr_status.wbmz_capacity_percent,
        "Capacity should match energy formula at V_min + 75%% of range"
    );
}

// Сценарий: Очень маленькое изменение напряжения между опросами
// Ожидается: Ток = 0 (ниже порога детекции) через regmap
static void test_supercap_current_zero_when_voltage_change_small(void)
{
    LOG_INFO("Testing supercap current zeroing for small voltage changes");

    utest_wbmz6_battery_set_present(false);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, TEST_NOMINAL_VOLTAGE_MV);

    // Первый вызов для инициализации
    wbmz_subsystem_do_periodic_work();

    // Продвинуть время для следующего опроса
    utest_systick_advance_time_ms(WBEC_WBMZ6_POLL_PERIOD_MS);

    // Очень маленькое изменение напряжения (1 mV)
    // Это даст ток меньше WBEC_WBMZ6_SUPERCAP_CURRENT_ZEROING_MA (80 mA)
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, TEST_NOMINAL_VOLTAGE_MV + 1);
    wbmz_subsystem_do_periodic_work();

    struct REGMAP_PWR_STATUS pwr_status = {};
    utest_regmap_get_region_data(REGMAP_REGION_PWR_STATUS, &pwr_status, sizeof(pwr_status));

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, pwr_status.wbmz_charging_current,
        "Charging current should be 0 for very small voltage changes"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, pwr_status.wbmz_discharging_current,
        "Discharging current should be 0 for very small voltage changes"
    );
}

// Сценарий: Напряжение растет между опросами
// Ожидается: Положительный ток зарядки, is_charging = 1 через regmap
static void test_supercap_charging_when_voltage_increasing(void)
{
    LOG_INFO("Testing supercap charging status when voltage increasing");

    utest_wbmz6_battery_set_present(false);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, TEST_NOMINAL_VOLTAGE_MV);

    // Первый вызов для инициализации
    wbmz_subsystem_do_periodic_work();

    // Продвинуть время для следующего опроса
    utest_systick_advance_time_ms(WBEC_WBMZ6_POLL_PERIOD_MS);

    // Увеличить напряжение значительно для получения тока > 80 mA
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, TEST_NOMINAL_VOLTAGE_MV + 100);
    wbmz_subsystem_do_periodic_work();

    struct REGMAP_PWR_STATUS pwr_status = {};
    utest_regmap_get_region_data(REGMAP_REGION_PWR_STATUS, &pwr_status, sizeof(pwr_status));

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        1, pwr_status.wbmz_is_charging,
        "Should report charging when voltage increasing"
    );
    TEST_ASSERT_GREATER_THAN_UINT16_MESSAGE(
        0, pwr_status.wbmz_charging_current,
        "Charging current should be positive"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, pwr_status.wbmz_discharging_current,
        "Discharging current should be 0 during charging"
    );
}

// Сценарий: Напряжение падает между опросами
// Ожидается: Положительный ток разрядки, is_charging = 0 через regmap
static void test_supercap_discharging_when_voltage_decreasing(void)
{
    LOG_INFO("Testing supercap discharging status when voltage decreasing");

    utest_wbmz6_battery_set_present(false);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, TEST_NOMINAL_VOLTAGE_MV + 100);

    // Первый вызов для инициализации
    wbmz_subsystem_do_periodic_work();

    // Продвинуть время для следующего опроса
    utest_systick_advance_time_ms(WBEC_WBMZ6_POLL_PERIOD_MS);

    // Уменьшить напряжение значительно для получения тока > 80 mA
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, TEST_NOMINAL_VOLTAGE_MV);
    wbmz_subsystem_do_periodic_work();

    struct REGMAP_PWR_STATUS pwr_status = {};
    utest_regmap_get_region_data(REGMAP_REGION_PWR_STATUS, &pwr_status, sizeof(pwr_status));

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, pwr_status.wbmz_is_charging,
        "Should NOT report charging when voltage decreasing"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, pwr_status.wbmz_charging_current,
        "Charging current should be 0 during discharging"
    );
    TEST_ASSERT_GREATER_THAN_UINT16_MESSAGE(
        0, pwr_status.wbmz_discharging_current,
        "Discharging current should be positive"
    );
}

// Сценарий: Изменение напряжения близко к порогу детекции тока (81 mA)
// Ожидается: Ток НЕ обнулён, так как выше CURRENT_ZEROING_MA (80 mA)
static void test_supercap_current_at_threshold_boundary(void)
{
    LOG_INFO("Testing supercap current near zeroing threshold");

    utest_wbmz6_battery_set_present(false);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, TEST_NOMINAL_VOLTAGE_MV);

    // Первый вызов для инициализации
    wbmz_subsystem_do_periodic_work();

    // Продвинуть время для следующего опроса
    utest_systick_advance_time_ms(WBEC_WBMZ6_POLL_PERIOD_MS);

    // Изменение напряжения для получения тока чуть выше порога (81 mA)
    // i = C * dV/dt = 25 F * (dV mV) / (poll_period ms)
    // 81 mA = 25000 mF * dV / 2000 ms => dV ≈ 6.5 mV
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, TEST_NOMINAL_VOLTAGE_MV + 7);
    wbmz_subsystem_do_periodic_work();

    struct REGMAP_PWR_STATUS pwr_status = {};
    utest_regmap_get_region_data(REGMAP_REGION_PWR_STATUS, &pwr_status, sizeof(pwr_status));

    TEST_ASSERT_GREATER_THAN_UINT16_MESSAGE(
        0, pwr_status.wbmz_charging_current,
        "Current should NOT be zeroed when above threshold"
    );
}

// Сценарий: Постоянное изменение напряжения на протяжении нескольких опросов
// Ожидается: Lowpass фильтр сходится к установившемуся значению тока
static void test_supercap_lowpass_filter_convergence(void)
{
    LOG_INFO("Testing supercap lowpass filter convergence over multiple polls");

    utest_wbmz6_battery_set_present(false);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, TEST_NOMINAL_VOLTAGE_MV);

    // Первый вызов для инициализации
    wbmz_subsystem_do_periodic_work();

    struct REGMAP_PWR_STATUS pwr_status = {};
    int positive_current_count = 0;

    // Последовательность из 8 опросов с постоянным dV = 10 mV
    for (int i = 0; i < 8; i++) {
        utest_systick_advance_time_ms(WBEC_WBMZ6_POLL_PERIOD_MS);
        utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, TEST_NOMINAL_VOLTAGE_MV + (i + 1) * 10);
        wbmz_subsystem_do_periodic_work();

        utest_regmap_get_region_data(REGMAP_REGION_PWR_STATUS, &pwr_status, sizeof(pwr_status));

        if (pwr_status.wbmz_charging_current > 0) {
            positive_current_count++;
        }
    }

    // Фильтр должен показывать положительный ток на всех итерациях
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        8, positive_current_count,
        "Lowpass filter should produce positive current on all iterations"
    );

    // На последней итерации ток должен быть достаточно большим (фильтр сошёлся)
    TEST_ASSERT_GREATER_THAN_UINT16_MESSAGE(
        100, pwr_status.wbmz_charging_current,
        "Current should be significant after filter convergence"
    );
}

// Сценарий: Напряжение растёт, затем падает, затем снова растёт
// Ожидается: Корректная смена направления тока и флага is_charging
static void test_supercap_current_direction_changes(void)
{
    LOG_INFO("Testing supercap current direction changes");

    utest_wbmz6_battery_set_present(false);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, TEST_NOMINAL_VOLTAGE_MV);

    // Инициализация
    wbmz_subsystem_do_periodic_work();
    struct REGMAP_PWR_STATUS pwr_status = {};

    // Фаза 1: Зарядка (рост напряжения)
    utest_systick_advance_time_ms(WBEC_WBMZ6_POLL_PERIOD_MS);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, TEST_NOMINAL_VOLTAGE_MV + 100);
    wbmz_subsystem_do_periodic_work();
    utest_regmap_get_region_data(REGMAP_REGION_PWR_STATUS, &pwr_status, sizeof(pwr_status));

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        1, pwr_status.wbmz_is_charging,
        "Phase 1: Should be charging"
    );
    TEST_ASSERT_GREATER_THAN_UINT16_MESSAGE(
        0, pwr_status.wbmz_charging_current,
        "Phase 1: Charging current > 0"
    );

    // Фаза 2: Разрядка (падение напряжения)
    utest_systick_advance_time_ms(WBEC_WBMZ6_POLL_PERIOD_MS);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, TEST_NOMINAL_VOLTAGE_MV);
    wbmz_subsystem_do_periodic_work();
    utest_regmap_get_region_data(REGMAP_REGION_PWR_STATUS, &pwr_status, sizeof(pwr_status));

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, pwr_status.wbmz_is_charging,
        "Phase 2: Should NOT be charging"
    );
    TEST_ASSERT_GREATER_THAN_UINT16_MESSAGE(
        0, pwr_status.wbmz_discharging_current,
        "Phase 2: Discharging current > 0"
    );

    // Фаза 3: Снова зарядка (рост напряжения)
    utest_systick_advance_time_ms(WBEC_WBMZ6_POLL_PERIOD_MS);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, TEST_NOMINAL_VOLTAGE_MV + 100);
    wbmz_subsystem_do_periodic_work();
    utest_regmap_get_region_data(REGMAP_REGION_PWR_STATUS, &pwr_status, sizeof(pwr_status));

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        1, pwr_status.wbmz_is_charging,
        "Phase 3: Should be charging again"
    );
    TEST_ASSERT_GREATER_THAN_UINT16_MESSAGE(
        0, pwr_status.wbmz_charging_current,
        "Phase 3: Charging current > 0"
    );
}

// ============================================================================
// Тесты wbmz-subsystem: Определение устройства
// ============================================================================

// Сценарий: Только суперконденсатор присутствует
// Ожидается: Обнаружен supercap, флаг в regmap установлен
static void test_subsystem_detects_supercap_only(void)
{
    LOG_INFO("Testing subsystem detects supercap only");

    // Батарея отсутствует, суперконденсатор присутствует
    utest_wbmz6_battery_set_present(false);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, WBEC_WBMZ6_SUPERCAP_DETECT_VOLTAGE_MV + 1000);

    wbmz_subsystem_do_periodic_work();

    struct REGMAP_PWR_STATUS pwr_status = {};
    bool result = utest_regmap_get_region_data(
        REGMAP_REGION_PWR_STATUS,
        &pwr_status,
        sizeof(pwr_status)
    );

    TEST_ASSERT_TRUE_MESSAGE(result, "Should read regmap data");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        1, pwr_status.wbmz_supercap_present,
        "Supercap present flag should be set"
    );
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, pwr_status.wbmz_battery_present,
        "Battery present flag should NOT be set"
    );
}

// Сценарий: Ни батарея, ни суперконденсатор не обнаружены
// Ожидается: Оба флага сброшены в regmap
static void test_subsystem_no_device_detected(void)
{
    LOG_INFO("Testing subsystem with no device detected");

    utest_wbmz6_battery_set_present(false);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, 0);

    wbmz_subsystem_do_periodic_work();

    struct REGMAP_PWR_STATUS pwr_status = {};
    bool result = utest_regmap_get_region_data(
        REGMAP_REGION_PWR_STATUS,
        &pwr_status,
        sizeof(pwr_status)
    );

    TEST_ASSERT_TRUE_MESSAGE(result, "Should read regmap data");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, pwr_status.wbmz_supercap_present,
        "Supercap present flag should NOT be set"
    );
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, pwr_status.wbmz_battery_present,
        "Battery present flag should NOT be set"
    );
}

// Сценарий: Батарея имеет приоритет над суперконденсатором
// Ожидается: Обнаружена батарея, суперконденсатор игнорируется
static void test_subsystem_battery_has_priority_over_supercap(void)
{
    LOG_INFO("Testing battery priority over supercap");

    // Оба устройства присутствуют
    utest_wbmz6_battery_set_present(true);
    utest_wbmz6_battery_set_init_result(true);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, WBEC_WBMZ6_SUPERCAP_DETECT_VOLTAGE_MV + 1000);

    wbmz_subsystem_do_periodic_work();

    struct REGMAP_PWR_STATUS pwr_status = {};
    bool result = utest_regmap_get_region_data(
        REGMAP_REGION_PWR_STATUS,
        &pwr_status,
        sizeof(pwr_status)
    );

    TEST_ASSERT_TRUE_MESSAGE(result, "Should read regmap data");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        1, pwr_status.wbmz_battery_present,
        "Battery present flag should be set"
    );
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, pwr_status.wbmz_supercap_present,
        "Supercap present flag should NOT be set (battery has priority)"
    );
    TEST_ASSERT_TRUE_MESSAGE(
        utest_wbmz6_battery_was_init_called(),
        "Battery init should have been called"
    );
}

// ============================================================================
// Тесты wbmz-subsystem: Периодический опрос
// ============================================================================

// Сценарий: Два вызова подряд без задержки
// Ожидается: Второй вызов не выполняет опрос (период не прошел)
static void test_subsystem_respects_poll_period(void)
{
    LOG_INFO("Testing subsystem poll period enforcement");

    utest_wbmz6_battery_set_present(false);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, WBEC_WBMZ6_SUPERCAP_DETECT_VOLTAGE_MV + 1);

    // Первый вызов
    wbmz_subsystem_do_periodic_work();

    // Изменим напряжение и сразу вызовем снова
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, 0);
    wbmz_subsystem_do_periodic_work();

    // Supercap должен остаться обнаруженным, т.к. период не прошел
    struct REGMAP_PWR_STATUS pwr_status = {};
    utest_regmap_get_region_data(
        REGMAP_REGION_PWR_STATUS,
        &pwr_status,
        sizeof(pwr_status)
    );

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        1, pwr_status.wbmz_supercap_present,
        "Supercap should still be detected (poll period not elapsed)"
    );
}

// Сценарий: Вызов после истечения периода
// Ожидается: Опрос выполняется, изменения обнаружены
static void test_subsystem_polls_after_period_elapsed(void)
{
    LOG_INFO("Testing subsystem polls after period");

    utest_wbmz6_battery_set_present(false);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, WBEC_WBMZ6_SUPERCAP_DETECT_VOLTAGE_MV + 1);

    // Первый вызов
    wbmz_subsystem_do_periodic_work();

    // Продвинуть время
    utest_systick_advance_time_ms(WBEC_WBMZ6_POLL_PERIOD_MS);

    // Удалить суперконденсатор
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, 0);
    wbmz_subsystem_do_periodic_work();

    // Теперь supercap должен быть не обнаружен
    struct REGMAP_PWR_STATUS pwr_status = {};
    utest_regmap_get_region_data(
        REGMAP_REGION_PWR_STATUS,
        &pwr_status,
        sizeof(pwr_status)
    );

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, pwr_status.wbmz_supercap_present,
        "Supercap should be removed after poll period"
    );
}

// ============================================================================
// Тесты wbmz-subsystem: Передача данных в regmap
// ============================================================================

// Сценарий: Суперконденсатор инициализирован и данные обновлены
// Ожидается: В regmap корректно записаны параметры и статус
static void test_subsystem_writes_supercap_data_to_regmap(void)
{
    LOG_INFO("Testing subsystem writes supercap data to regmap");

    utest_wbmz6_battery_set_present(false);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, TEST_NOMINAL_VOLTAGE_MV);

    wbmz_subsystem_do_periodic_work();

    struct REGMAP_PWR_STATUS pwr_status = {};
    bool result = utest_regmap_get_region_data(
        REGMAP_REGION_PWR_STATUS,
        &pwr_status,
        sizeof(pwr_status)
    );

    TEST_ASSERT_TRUE_MESSAGE(result, "Should read regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        TEST_NOMINAL_VOLTAGE_MV, pwr_status.wbmz_battery_voltage,
        "Voltage should be written to regmap"
    );
    TEST_ASSERT_GREATER_THAN_UINT16_MESSAGE(
        0, pwr_status.wbmz_full_design_capacity,
        "Design capacity should be written"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        WBEC_WBMZ6_SUPERCAP_VOLTAGE_MIN_MV, pwr_status.wbmz_voltage_min_design,
        "Min voltage should be written"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        WBEC_WBMZ6_SUPERCAP_VOLTAGE_MAX_MV, pwr_status.wbmz_voltage_max_design,
        "Max voltage should be written"
    );
}

#endif // WBEC_WBMZ6_SUPPORT

// ============================================================================
// Тесты wbmz-subsystem: Базовая функциональность (для всех таргетов)
// ============================================================================

// Сценарий: Проверка интеграции с wbmz-common
// Ожидается: Данные от wbmz-common корректно записаны в regmap
static void test_subsystem_integrates_wbmz_common_data(void)
{
    LOG_INFO("Testing subsystem integration with wbmz-common");

    utest_wbmz_set_powered_from_wbmz(true);
    utest_set_wbmz_stepup_enabled(true);

    wbmz_subsystem_do_periodic_work();

    struct REGMAP_PWR_STATUS pwr_status = {};
    utest_regmap_get_region_data(
        REGMAP_REGION_PWR_STATUS,
        &pwr_status,
        sizeof(pwr_status)
    );

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        1, pwr_status.powered_from_wbmz,
        "powered_from_wbmz should be set from wbmz-common"
    );
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        1, pwr_status.wbmz_stepup_enabled,
        "stepup_enabled should be set from wbmz-common"
    );
}

// Сценарий: Вызов subsystem без WBMZ6 поддержки (WB74)
// Ожидается: Функция работает корректно, флаги WBMZ6 остаются в 0
static void test_subsystem_without_wbmz6_support(void)
{
    LOG_INFO("Testing subsystem without WBMZ6 support");

    wbmz_subsystem_do_periodic_work();

    struct REGMAP_PWR_STATUS pwr_status = {};
    utest_regmap_get_region_data(
        REGMAP_REGION_PWR_STATUS,
        &pwr_status,
        sizeof(pwr_status)
    );

#if defined(WBEC_WBMZ6_SUPPORT)
    // На WB85 можно ожидать значения 0, если устройства не обнаружены
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, pwr_status.wbmz_battery_present,
        "Battery present should be 0 when not detected"
    );
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, pwr_status.wbmz_supercap_present,
        "Supercap present should be 0 when not detected"
    );
#else
    // На WB74 все флаги WBMZ6 всегда 0
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, pwr_status.wbmz_battery_present,
        "Battery present should be 0 on WB74 (no WBMZ6 support)"
    );
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, pwr_status.wbmz_supercap_present,
        "Supercap present should be 0 on WB74 (no WBMZ6 support)"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, pwr_status.wbmz_full_design_capacity,
        "Design capacity should be 0 on WB74 (no WBMZ6 support)"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, pwr_status.wbmz_battery_voltage,
        "Battery voltage should be 0 on WB74 (no WBMZ6 support)"
    );
#endif
}

int main(void)
{
    UNITY_BEGIN();

#if defined(WBEC_WBMZ6_SUPPORT)
    // Тесты wbmz6-supercap: Определение наличия
    RUN_TEST(test_supercap_is_present_when_voltage_above_threshold);
    RUN_TEST(test_supercap_not_present_when_voltage_below_threshold);

    // Тесты wbmz6-supercap: Инициализация
    RUN_TEST(test_supercap_init_sets_correct_params);

    // Тесты wbmz6-supercap: Обновление статуса
    RUN_TEST(test_supercap_status_at_max_voltage);
    RUN_TEST(test_supercap_status_at_min_voltage);
    RUN_TEST(test_supercap_dead_when_voltage_below_min);
    RUN_TEST(test_supercap_capacity_capped_when_voltage_above_max);
    RUN_TEST(test_supercap_capacity_calculated_correctly_at_intermediate_voltages);
    RUN_TEST(test_supercap_current_zero_when_voltage_change_small);
    RUN_TEST(test_supercap_charging_when_voltage_increasing);
    RUN_TEST(test_supercap_discharging_when_voltage_decreasing);
    RUN_TEST(test_supercap_current_at_threshold_boundary);
    RUN_TEST(test_supercap_lowpass_filter_convergence);
    RUN_TEST(test_supercap_current_direction_changes);

    // Тесты wbmz-subsystem: Определение устройства
    RUN_TEST(test_subsystem_detects_supercap_only);
    RUN_TEST(test_subsystem_no_device_detected);
    RUN_TEST(test_subsystem_battery_has_priority_over_supercap);

    // Тесты wbmz-subsystem: Периодический опрос
    RUN_TEST(test_subsystem_respects_poll_period);
    RUN_TEST(test_subsystem_polls_after_period_elapsed);

    // Тесты wbmz-subsystem: Передача данных в regmap
    RUN_TEST(test_subsystem_writes_supercap_data_to_regmap);
#endif

    // Тесты wbmz-subsystem: Базовая функциональность (для всех таргетов)
    RUN_TEST(test_subsystem_integrates_wbmz_common_data);
    RUN_TEST(test_subsystem_without_wbmz6_support);

    return UNITY_END();
}
