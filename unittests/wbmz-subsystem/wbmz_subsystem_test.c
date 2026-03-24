#include "unity.h"
#include "config.h"
#include "wbmz-subsystem.h"
#include "regmap-int.h"
#include "regmap-structs.h"

#if defined(WBEC_WBMZ6_SUPPORT)
#include "wbmz6-supercap.h"
#include "wbmz6-status.h"
#endif

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
// Ожидается: wbmz6_supercap_is_present() возвращает true
static void test_supercap_is_present_when_voltage_above_threshold(void)
{
    LOG_INFO("Testing supercap detection: voltage above threshold");

    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, WBEC_WBMZ6_SUPERCAP_DETECT_VOLTAGE_MV + 100);

    bool present = wbmz6_supercap_is_present();

    TEST_ASSERT_TRUE_MESSAGE(
        present,
        "Supercap should be detected when voltage is above threshold"
    );
}

// Сценарий: ADC возвращает напряжение ниже порога обнаружения
// Ожидается: wbmz6_supercap_is_present() возвращает false
static void test_supercap_not_present_when_voltage_below_threshold(void)
{
    LOG_INFO("Testing supercap detection: voltage below threshold");

    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, WBEC_WBMZ6_SUPERCAP_DETECT_VOLTAGE_MV - 100);

    bool present = wbmz6_supercap_is_present();

    TEST_ASSERT_FALSE_MESSAGE(
        present,
        "Supercap should NOT be detected when voltage is below threshold"
    );
}

// ============================================================================
// Тесты wbmz6-supercap: Инициализация
// ============================================================================

// Сценарий: Вызов wbmz6_supercap_init() с заданным напряжением
// Ожидается: Корректная инициализация параметров
static void test_supercap_init_sets_correct_params(void)
{
    LOG_INFO("Testing supercap initialization");

    struct wbmz6_params params = {};
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, 4000);

    wbmz6_supercap_init(&params);

    TEST_ASSERT_GREATER_THAN_UINT16_MESSAGE(
        0, params.full_design_capacity_mah,
        "Design capacity should be set"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        WBEC_WBMZ6_SUPERCAP_VOLTAGE_MIN_MV, params.voltage_min_mv,
        "Min voltage should match config"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        WBEC_WBMZ6_SUPERCAP_VOLTAGE_MAX_MV, params.voltage_max_mv,
        "Max voltage should match config"
    );
    TEST_ASSERT_GREATER_THAN_UINT16_MESSAGE(
        0, params.charge_current_ma,
        "Charge current should be set"
    );
}

// ============================================================================
// Тесты wbmz6-supercap: Обновление статуса
// ============================================================================

// Сценарий: Напряжение на максимуме
// Ожидается: capacity_percent = 100%, is_dead = false
static void test_supercap_status_at_max_voltage(void)
{
    LOG_INFO("Testing supercap status at max voltage");

    struct wbmz6_params params = {};
    struct wbmz6_status status = {};

    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, WBEC_WBMZ6_SUPERCAP_VOLTAGE_MAX_MV);
    wbmz6_supercap_init(&params);
    wbmz6_supercap_update_status(&status);

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        WBEC_WBMZ6_SUPERCAP_VOLTAGE_MAX_MV, status.voltage_now_mv,
        "Voltage should match ADC reading"
    );
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        100, status.capacity_percent,
        "Capacity should be 100% at max voltage"
    );
    TEST_ASSERT_FALSE_MESSAGE(
        status.is_dead,
        "Supercap should not be dead at max voltage"
    );
    TEST_ASSERT_TRUE_MESSAGE(
        status.is_inserted,
        "Supercap should always report as inserted"
    );
}

// Сценарий: Напряжение на минимуме
// Ожидается: capacity_percent = 0%
static void test_supercap_status_at_min_voltage(void)
{
    LOG_INFO("Testing supercap status at min voltage");

    struct wbmz6_params params = {};
    struct wbmz6_status status = {};

    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, WBEC_WBMZ6_SUPERCAP_VOLTAGE_MIN_MV);
    wbmz6_supercap_init(&params);
    wbmz6_supercap_update_status(&status);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, status.capacity_percent,
        "Capacity should be 0% at min voltage"
    );
}

// Сценарий: Напряжение ниже минимума
// Ожидается: is_dead = true
static void test_supercap_dead_when_voltage_below_min(void)
{
    LOG_INFO("Testing supercap dead flag below min voltage");

    struct wbmz6_params params = {};
    struct wbmz6_status status = {};

    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, WBEC_WBMZ6_SUPERCAP_VOLTAGE_MIN_MV - 100);
    wbmz6_supercap_init(&params);
    wbmz6_supercap_update_status(&status);

    TEST_ASSERT_TRUE_MESSAGE(
        status.is_dead,
        "Supercap should be marked as dead when voltage below minimum"
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
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, WBEC_WBMZ6_SUPERCAP_DETECT_VOLTAGE_MV + 1000);

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
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, WBEC_WBMZ6_SUPERCAP_DETECT_VOLTAGE_MV + 1000);

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
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBAT, 4000);

    wbmz_subsystem_do_periodic_work();

    struct REGMAP_PWR_STATUS pwr_status = {};
    bool result = utest_regmap_get_region_data(
        REGMAP_REGION_PWR_STATUS,
        &pwr_status,
        sizeof(pwr_status)
    );

    TEST_ASSERT_TRUE_MESSAGE(result, "Should read regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        4000, pwr_status.wbmz_battery_voltage,
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
    TEST_ASSERT_EQUAL_UINT8(0, pwr_status.wbmz_battery_present);
    TEST_ASSERT_EQUAL_UINT8(0, pwr_status.wbmz_supercap_present);
    TEST_ASSERT_EQUAL_UINT16(0, pwr_status.wbmz_full_design_capacity);
    TEST_ASSERT_EQUAL_UINT16(0, pwr_status.wbmz_battery_voltage);
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
