#include "unity.h"
#include <stddef.h>
#include "config.h"
#include "mcu-vbat.h"
#include "regmap-int.h"
#include "regmap-structs.h"
#include "wbmcu_system.h"
#include "adc.h"

// Test helpers
#include "utest_adc.h"
#include "utest_systick.h"
#include "utest_regmap.h"
#include "utest_wbmcu_system.h"
#include "utest_temperature_control.h"

/**
 * Тесты алгоритма обслуживания батарейки RTC (mcu-vbat.c)
 *
 * Константы синхронизированы с mcu-vbat.c
 */
#define TEST_DELTAV_TEST_PERIOD_MS      (1*24*3600*1000)
#define TEST_MEAS_STABILIZE_MS          30
#define TEST_DELTAV_SETTLE_MS           (30*1000)
#define TEST_CHARGE_BURST_MS            (55*60*1000)
#define TEST_CHARGE_PAUSE_MS            (5*60*1000)
#define TEST_CHARGE_TIMEOUT_MS          (96*3600*1000)

// Типовые напряжения узла VBAT, мВ
#define TEST_V1_FULL_MV                 3000    // OCV полной батарейки
#define TEST_V2_FULL_MV                 3170    // узел под зарядом, полная (ΔV = 170)
#define TEST_V1_DISCHARGED_MV           2700    // разряженная (диодный пол)
#define TEST_V2_DISCHARGED_MV           3070    // узел под зарядом, разряженная (ΔV = 370)
#define TEST_V1_NO_BATTERY_MV           2650    // нет батарейки: узел на диодном полу
#define TEST_V2_NO_BATTERY_MV           3290    // нет батарейки: узел поднялся к VDD (ΔV = 640)
#define TEST_OCV_NOT_CHARGED_MV         2900    // OCV в паузе, заряд не завершён
#define TEST_OCV_CHARGED_MV             3060    // OCV в паузе, заряд завершён

static bool is_vbe_on(void)
{
    return (PWR->CR4 & PWR_CR4_VBE) != 0;
}

static struct REGMAP_VBAT_STATUS get_status(void)
{
    struct REGMAP_VBAT_STATUS r = {};
    utest_regmap_get_region_data(REGMAP_REGION_VBAT_STATUS, &r, sizeof(r));
    return r;
}

static void run(void)
{
    mcu_vbat_check_do_periodic_work();
}

static void advance_and_run(uint32_t ms)
{
    utest_systick_advance_time_ms(ms);
    run();
}

// Проводит ΔV-тест с заданными V1 и V2.
// На входе алгоритм должен быть в IDLE с истекшим периодом.
// На выходе алгоритм принял решение по ΔV.
static void do_deltav_test(int32_t v1_mv, int32_t v2_mv)
{
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_INT_VBAT, v1_mv);
    run();  // IDLE -> TEST_V1, делитель включён
    TEST_ASSERT_TRUE(utest_adc_is_int_vbat_divider_enabled());
    advance_and_run(TEST_MEAS_STABILIZE_MS);    // TEST_V1 -> TEST_V2, V1 защёлкнуто, VBE включён
    TEST_ASSERT_TRUE(is_vbe_on());
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_INT_VBAT, v2_mv);
    advance_and_run(TEST_DELTAV_SETTLE_MS);     // TEST_V2 -> решение
}

void setUp(void)
{
    utest_pwr_reset();
    utest_regmap_reset();
    utest_systick_set_time_ms(1000000);
    utest_temperature_control_set_temperature_c_x100(2500);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_INT_VBAT, TEST_V1_FULL_MV);
    adc_int_vbat_divider_enable(false);
    mcu_vbat_init();
}

void tearDown(void) {}

void test_regmap_vbat_status_layout(void)
{
    // Структуры regmap упакованы (packed), и битфилды в них занимают
    // минимум байтов: если не добить слово с флагами до 16 бит анонимным
    // битфилдом, все последующие поля сдвинутся относительно регистровой
    // сетки (регистры 16-битные)
    TEST_ASSERT_EQUAL(6, sizeof(struct REGMAP_VBAT_STATUS));
    TEST_ASSERT_EQUAL(0, offsetof(struct REGMAP_VBAT_STATUS, voltage_mv));
    TEST_ASSERT_EQUAL(4, offsetof(struct REGMAP_VBAT_STATUS, delta_mv));
}

void test_init_selects_1k5_charging_resistor(void)
{
    TEST_ASSERT_TRUE((PWR->CR4 & PWR_CR4_VBRS) != 0);
    TEST_ASSERT_FALSE(is_vbe_on());
}

void test_full_battery_does_not_start_charging(void)
{
    do_deltav_test(TEST_V1_FULL_MV, TEST_V2_FULL_MV);

    // ΔV = 170 < 300: батарейка заряжена, зарядка не нужна
    TEST_ASSERT_FALSE(is_vbe_on());
    TEST_ASSERT_FALSE(utest_adc_is_int_vbat_divider_enabled());

    struct REGMAP_VBAT_STATUS r = get_status();
    TEST_ASSERT_EQUAL(TEST_V1_FULL_MV, r.voltage_mv);
    TEST_ASSERT_EQUAL(TEST_V2_FULL_MV - TEST_V1_FULL_MV, r.delta_mv);
    TEST_ASSERT_EQUAL(0, r.is_charging);
    TEST_ASSERT_EQUAL(0, r.battery_absent);
}

void test_next_deltav_test_starts_after_period(void)
{
    do_deltav_test(TEST_V1_FULL_MV, TEST_V2_FULL_MV);
    TEST_ASSERT_FALSE(is_vbe_on());

    // Через час теста ещё нет
    advance_and_run(3600*1000);
    TEST_ASSERT_FALSE(utest_adc_is_int_vbat_divider_enabled());

    // Через сутки тест запускается
    advance_and_run(TEST_DELTAV_TEST_PERIOD_MS);
    TEST_ASSERT_TRUE(utest_adc_is_int_vbat_divider_enabled());
}

void test_discharged_battery_charges_until_ocv_reached(void)
{
    do_deltav_test(TEST_V1_DISCHARGED_MV, TEST_V2_DISCHARGED_MV);

    // ΔV = 370 >= 300: большой ток принятия заряда, зарядка продолжается
    TEST_ASSERT_TRUE(is_vbe_on());
    TEST_ASSERT_FALSE(utest_adc_is_int_vbat_divider_enabled());
    TEST_ASSERT_EQUAL(1, get_status().is_charging);
    TEST_ASSERT_EQUAL(0, get_status().battery_absent);

    // Конец бёрста: пауза, заряд выключен, делитель включён для замера OCV
    advance_and_run(TEST_CHARGE_BURST_MS);
    TEST_ASSERT_FALSE(is_vbe_on());
    TEST_ASSERT_TRUE(utest_adc_is_int_vbat_divider_enabled());

    // OCV в конце паузы ниже отсечки: зарядка продолжается
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_INT_VBAT, TEST_OCV_NOT_CHARGED_MV);
    advance_and_run(TEST_CHARGE_PAUSE_MS);
    TEST_ASSERT_TRUE(is_vbe_on());
    TEST_ASSERT_EQUAL(TEST_OCV_NOT_CHARGED_MV, get_status().voltage_mv);

    // Ещё один бёрст, в паузе OCV выше отсечки: зарядка завершена
    advance_and_run(TEST_CHARGE_BURST_MS);
    TEST_ASSERT_FALSE(is_vbe_on());
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_INT_VBAT, TEST_OCV_CHARGED_MV);
    advance_and_run(TEST_CHARGE_PAUSE_MS);
    TEST_ASSERT_FALSE(is_vbe_on());
    TEST_ASSERT_FALSE(utest_adc_is_int_vbat_divider_enabled());
    TEST_ASSERT_EQUAL(TEST_OCV_CHARGED_MV, get_status().voltage_mv);
    TEST_ASSERT_EQUAL(0, get_status().is_charging);
}

void test_missing_battery_sets_flag_and_does_not_charge(void)
{
    do_deltav_test(TEST_V1_NO_BATTERY_MV, TEST_V2_NO_BATTERY_MV);

    // ΔV = 640 >= 550: ток заряда отсутствует, батарейки нет
    TEST_ASSERT_FALSE(is_vbe_on());
    TEST_ASSERT_EQUAL(1, get_status().battery_absent);
    TEST_ASSERT_EQUAL(0, get_status().is_charging);

    // Батарейку вставили: следующий тест снимает флаг
    advance_and_run(TEST_DELTAV_TEST_PERIOD_MS);
    do_deltav_test(TEST_V1_FULL_MV, TEST_V2_FULL_MV);
    TEST_ASSERT_EQUAL(0, get_status().battery_absent);
}

void test_charging_stops_by_total_time_cap(void)
{
    do_deltav_test(TEST_V1_DISCHARGED_MV, TEST_V2_DISCHARGED_MV);
    TEST_ASSERT_TRUE(is_vbe_on());

    // OCV никогда не достигает отсечки (деградировавшая батарейка)
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_INT_VBAT, TEST_OCV_NOT_CHARGED_MV);

    // Крутим бёрсты, пока суммарное время заряда не достигнет капа
    uint32_t bursts = (TEST_CHARGE_TIMEOUT_MS / TEST_CHARGE_BURST_MS) + 1;
    for (uint32_t i = 0; i < bursts; i++) {
        TEST_ASSERT_TRUE(is_vbe_on());
        advance_and_run(TEST_CHARGE_BURST_MS);      // бёрст -> пауза
        advance_and_run(TEST_CHARGE_PAUSE_MS);      // пауза -> решение
    }

    // Кап достигнут: зарядка остановлена несмотря на низкое OCV
    TEST_ASSERT_FALSE(is_vbe_on());
    TEST_ASSERT_EQUAL(0, get_status().is_charging);
}

void test_deltav_test_is_blocked_by_temperature(void)
{
    utest_temperature_control_set_temperature_c_x100(-2000);   // -20°C, ниже -15°C

    run();
    TEST_ASSERT_FALSE(utest_adc_is_int_vbat_divider_enabled());
    TEST_ASSERT_FALSE(is_vbe_on());

    // Температура вернулась в допустимый диапазон: тест запускается
    utest_temperature_control_set_temperature_c_x100(-1000);   // -10°C
    run();
    TEST_ASSERT_TRUE(utest_adc_is_int_vbat_divider_enabled());
}

void test_charging_pauses_on_overtemperature_and_resumes_with_hysteresis(void)
{
    do_deltav_test(TEST_V1_DISCHARGED_MV, TEST_V2_DISCHARGED_MV);
    TEST_ASSERT_TRUE(is_vbe_on());

    // Перегрев в бёрсте: зарядка приостановлена
    utest_temperature_control_set_temperature_c_x100(5600);    // +56°C, выше +55°C
    advance_and_run(1000);
    TEST_ASSERT_FALSE(is_vbe_on());

    // Температура чуть снизилась, но гистерезис ещё не пройден
    utest_temperature_control_set_temperature_c_x100(5200);    // +52°C > +50°C
    advance_and_run(1000);
    TEST_ASSERT_FALSE(is_vbe_on());

    // Гистерезис пройден: зарядка продолжается
    utest_temperature_control_set_temperature_c_x100(4900);    // +49°C
    advance_and_run(1000);
    TEST_ASSERT_TRUE(is_vbe_on());
}

void test_stop_charging_for_standby(void)
{
    do_deltav_test(TEST_V1_DISCHARGED_MV, TEST_V2_DISCHARGED_MV);
    TEST_ASSERT_TRUE(is_vbe_on());

    mcu_vbat_stop_charging();

    TEST_ASSERT_FALSE(is_vbe_on());
    TEST_ASSERT_FALSE(utest_adc_is_int_vbat_divider_enabled());
}

void test_trigger_measurement_interrupts_charging_and_restarts_test(void)
{
    do_deltav_test(TEST_V1_DISCHARGED_MV, TEST_V2_DISCHARGED_MV);
    TEST_ASSERT_TRUE(is_vbe_on());

    mcu_vbat_trigger_measurement();
    TEST_ASSERT_FALSE(is_vbe_on());

    // Ближайший do_periodic_work запускает новый ΔV-тест
    run();
    TEST_ASSERT_TRUE(utest_adc_is_int_vbat_divider_enabled());
}

void test_restart_charging_forces_charge_cycle(void)
{
    do_deltav_test(TEST_V1_FULL_MV, TEST_V2_FULL_MV);
    TEST_ASSERT_FALSE(is_vbe_on());

    mcu_vbat_restart_charging();
    run();

    TEST_ASSERT_TRUE(is_vbe_on());
    TEST_ASSERT_EQUAL(1, get_status().is_charging);
}

void test_negative_deltav_is_treated_as_full_battery(void)
{
    // V2 < V1 (шум измерения): ΔV защёлкивается в 0, зарядка не запускается
    do_deltav_test(TEST_V1_FULL_MV, TEST_V1_FULL_MV - 20);

    TEST_ASSERT_FALSE(is_vbe_on());
    TEST_ASSERT_EQUAL(0, get_status().delta_mv);
    TEST_ASSERT_EQUAL(0, get_status().battery_absent);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_regmap_vbat_status_layout);
    RUN_TEST(test_init_selects_1k5_charging_resistor);
    RUN_TEST(test_full_battery_does_not_start_charging);
    RUN_TEST(test_next_deltav_test_starts_after_period);
    RUN_TEST(test_discharged_battery_charges_until_ocv_reached);
    RUN_TEST(test_missing_battery_sets_flag_and_does_not_charge);
    RUN_TEST(test_charging_stops_by_total_time_cap);
    RUN_TEST(test_deltav_test_is_blocked_by_temperature);
    RUN_TEST(test_charging_pauses_on_overtemperature_and_resumes_with_hysteresis);
    RUN_TEST(test_stop_charging_for_standby);
    RUN_TEST(test_trigger_measurement_interrupts_charging_and_restarts_test);
    RUN_TEST(test_restart_charging_forces_charge_cycle);
    RUN_TEST(test_negative_deltav_is_treated_as_full_battery);

    return UNITY_END();
}
