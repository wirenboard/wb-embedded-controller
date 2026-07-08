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
#define TEST_PERIOD_MS                  (1*24*3600*1000)
#define TEST_ON_MS                      (60*1000)
#define TEST_JUMP_MS                    60
#define TEST_CHARGE_BURST_MS            (55*60*1000)
#define TEST_CHARGE_PAUSE_MS            (5*60*1000)
#define TEST_CHARGE_TIMEOUT_MS          (96*3600*1000)
#define TEST_R_SENSE_OHM                3080

// Типовые напряжения узла VBAT, мВ (из эксперимента на WB8.5)
// Полная батарейка: I ~ 40 мкА
#define TEST_FULL_V2_MV                 3260
#define TEST_FULL_V2P_MV                3139    // скачок 121 мВ -> 39 мкА
// Разряженная батарейка (плато): I ~ 133 мкА
#define TEST_DISCHARGED_V2_MV           3070
#define TEST_DISCHARGED_V2P_MV          2660    // скачок 410 мВ -> 133 мкА
// Батарейка отсутствует: узел у рельсы, после снятия VBE падает на диодный пол
#define TEST_NO_BATT_V2_MV              3290
#define TEST_NO_BATT_V2P_MV             2660    // скачок 630 мВ -> 204 мкА
// OCV в паузах заряда
#define TEST_OCV_NOT_CHARGED_MV         2900
#define TEST_OCV_CHARGED_MV             3060

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

// Проводит тест тока принятия заряда с заданными V2 (под зарядом)
// и V2' (после снятия VBE).
// На входе алгоритм должен быть в IDLE с истекшим периодом.
// На выходе алгоритм принял решение.
static void do_current_test(int32_t v2_mv, int32_t v2p_mv)
{
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_INT_VBAT, v2_mv);
    run();  // IDLE -> TEST_ON: делитель и VBE включены
    TEST_ASSERT_TRUE(utest_adc_is_int_vbat_divider_enabled());
    TEST_ASSERT_TRUE(is_vbe_on());
    advance_and_run(TEST_ON_MS);        // TEST_ON -> TEST_JUMP: V2 защёлкнуто, VBE снят
    TEST_ASSERT_FALSE(is_vbe_on());
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_INT_VBAT, v2p_mv);
    advance_and_run(TEST_JUMP_MS);      // TEST_JUMP -> решение
}

void setUp(void)
{
    utest_pwr_reset();
    utest_regmap_reset();
    utest_systick_set_time_ms(1000000);
    utest_temperature_control_set_temperature_c_x100(2500);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_INT_VBAT, TEST_FULL_V2_MV);
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
    TEST_ASSERT_EQUAL(4, offsetof(struct REGMAP_VBAT_STATUS, charge_current_ua));
}

void test_init_selects_1k5_charging_resistor(void)
{
    TEST_ASSERT_TRUE((PWR->CR4 & PWR_CR4_VBRS) != 0);
    TEST_ASSERT_FALSE(is_vbe_on());
}

void test_full_battery_does_not_start_charging(void)
{
    do_current_test(TEST_FULL_V2_MV, TEST_FULL_V2P_MV);

    // 39 мкА < 100 мкА: батарейка заряжена, зарядка не нужна
    TEST_ASSERT_FALSE(is_vbe_on());
    TEST_ASSERT_FALSE(utest_adc_is_int_vbat_divider_enabled());

    struct REGMAP_VBAT_STATUS r = get_status();
    TEST_ASSERT_EQUAL((TEST_FULL_V2_MV - TEST_FULL_V2P_MV) * 1000 / TEST_R_SENSE_OHM,
                      r.charge_current_ua);
    TEST_ASSERT_EQUAL(TEST_FULL_V2P_MV, r.voltage_mv);
    TEST_ASSERT_EQUAL(0, r.is_charging);
    TEST_ASSERT_EQUAL(0, r.battery_absent);
}

void test_next_test_starts_after_period(void)
{
    do_current_test(TEST_FULL_V2_MV, TEST_FULL_V2P_MV);
    TEST_ASSERT_FALSE(is_vbe_on());

    // Через час теста ещё нет
    advance_and_run(3600*1000);
    TEST_ASSERT_FALSE(utest_adc_is_int_vbat_divider_enabled());

    // Через сутки тест запускается
    advance_and_run(TEST_PERIOD_MS);
    TEST_ASSERT_TRUE(utest_adc_is_int_vbat_divider_enabled());
    TEST_ASSERT_TRUE(is_vbe_on());
}

void test_discharged_battery_charges_until_ocv_reached(void)
{
    do_current_test(TEST_DISCHARGED_V2_MV, TEST_DISCHARGED_V2P_MV);

    // 133 мкА >= 100 мкА: батарейка разряжена, зарядка продолжается
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
    do_current_test(TEST_NO_BATT_V2_MV, TEST_NO_BATT_V2P_MV);

    // 204 мкА при V2 у рельсы: батарейки нет
    TEST_ASSERT_FALSE(is_vbe_on());
    TEST_ASSERT_EQUAL(1, get_status().battery_absent);
    TEST_ASSERT_EQUAL(0, get_status().is_charging);

    // Батарейку вставили: следующий тест снимает флаг
    advance_and_run(TEST_PERIOD_MS);
    do_current_test(TEST_FULL_V2_MV, TEST_FULL_V2P_MV);
    TEST_ASSERT_EQUAL(0, get_status().battery_absent);
}

void test_deeply_discharged_battery_is_not_mistaken_for_missing(void)
{
    // Большой ток, но узел под зарядом НЕ у рельсы: это разряженная
    // батарейка (скачок 840 мВ -> 272 мкА, V2 = 2940 < 3150)
    do_current_test(2940, 2100);

    TEST_ASSERT_EQUAL(0, get_status().battery_absent);
    TEST_ASSERT_TRUE(is_vbe_on());
    TEST_ASSERT_EQUAL(1, get_status().is_charging);
}

void test_charging_stops_by_total_time_cap(void)
{
    do_current_test(TEST_DISCHARGED_V2_MV, TEST_DISCHARGED_V2P_MV);
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

void test_current_test_is_blocked_by_temperature(void)
{
    utest_temperature_control_set_temperature_c_x100(-2000);   // -20°C, ниже -15°C

    run();
    TEST_ASSERT_FALSE(utest_adc_is_int_vbat_divider_enabled());
    TEST_ASSERT_FALSE(is_vbe_on());

    // Температура вернулась в допустимый диапазон: тест запускается
    utest_temperature_control_set_temperature_c_x100(-1000);   // -10°C
    run();
    TEST_ASSERT_TRUE(utest_adc_is_int_vbat_divider_enabled());
    TEST_ASSERT_TRUE(is_vbe_on());
}

void test_charging_pauses_on_overtemperature_and_resumes_with_hysteresis(void)
{
    do_current_test(TEST_DISCHARGED_V2_MV, TEST_DISCHARGED_V2P_MV);
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
    do_current_test(TEST_DISCHARGED_V2_MV, TEST_DISCHARGED_V2P_MV);
    TEST_ASSERT_TRUE(is_vbe_on());

    mcu_vbat_stop_charging();

    TEST_ASSERT_FALSE(is_vbe_on());
    TEST_ASSERT_FALSE(utest_adc_is_int_vbat_divider_enabled());
}

void test_trigger_measurement_interrupts_charging_and_restarts_test(void)
{
    do_current_test(TEST_DISCHARGED_V2_MV, TEST_DISCHARGED_V2P_MV);
    TEST_ASSERT_TRUE(is_vbe_on());

    mcu_vbat_trigger_measurement();
    TEST_ASSERT_FALSE(is_vbe_on());

    // Ближайший do_periodic_work запускает новый тест
    run();
    TEST_ASSERT_TRUE(utest_adc_is_int_vbat_divider_enabled());
    TEST_ASSERT_TRUE(is_vbe_on());
}

void test_restart_charging_forces_charge_cycle(void)
{
    do_current_test(TEST_FULL_V2_MV, TEST_FULL_V2P_MV);
    TEST_ASSERT_FALSE(is_vbe_on());

    mcu_vbat_restart_charging();
    run();

    TEST_ASSERT_TRUE(is_vbe_on());
    TEST_ASSERT_EQUAL(1, get_status().is_charging);
}

void test_negative_jump_is_treated_as_full_battery(void)
{
    // V2' > V2 (шум измерения): ток защёлкивается в 0, зарядка не запускается
    do_current_test(TEST_FULL_V2_MV, TEST_FULL_V2_MV + 20);

    TEST_ASSERT_FALSE(is_vbe_on());
    TEST_ASSERT_EQUAL(0, get_status().charge_current_ua);
    TEST_ASSERT_EQUAL(0, get_status().battery_absent);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_regmap_vbat_status_layout);
    RUN_TEST(test_init_selects_1k5_charging_resistor);
    RUN_TEST(test_full_battery_does_not_start_charging);
    RUN_TEST(test_next_test_starts_after_period);
    RUN_TEST(test_discharged_battery_charges_until_ocv_reached);
    RUN_TEST(test_missing_battery_sets_flag_and_does_not_charge);
    RUN_TEST(test_deeply_discharged_battery_is_not_mistaken_for_missing);
    RUN_TEST(test_charging_stops_by_total_time_cap);
    RUN_TEST(test_current_test_is_blocked_by_temperature);
    RUN_TEST(test_charging_pauses_on_overtemperature_and_resumes_with_hysteresis);
    RUN_TEST(test_stop_charging_for_standby);
    RUN_TEST(test_trigger_measurement_interrupts_charging_and_restarts_test);
    RUN_TEST(test_restart_charging_forces_charge_cycle);
    RUN_TEST(test_negative_jump_is_treated_as_full_battery);

    return UNITY_END();
}
