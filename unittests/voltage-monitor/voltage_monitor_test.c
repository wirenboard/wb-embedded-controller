#include "unity.h"
#include "voltage-monitor.h"
#include "adc.h"
#include "systick.h"
#include "utest_adc.h"
#include "utest_systick.h"

#define LOG_LEVEL LOG_LEVEL_INFO
#include "console_log.h"


void setUp(void)
{
    // Установка начального времени
    utest_systick_set_time_ms(1000);
}

void tearDown(void)
{

}

// Сценарий: Инициализация подсистемы мониторинга напряжения
// Ожидается: Состояние not ready до инициализации, not ready до истечения стартовой задержки,
// состояние ready после истечения задержки
static void test_vmon_init(void)
{
    LOG_INFO("Testing initialization");

    TEST_ASSERT_FALSE_MESSAGE(vmon_ready(), "voltage-monitor shold not be ready before vmon_init() call");

    vmon_init();
    TEST_ASSERT_FALSE_MESSAGE(vmon_ready(), "voltage-monitor shold not be ready right after vmon_init() call");

    utest_systick_advance_time_ms(VOLTAGE_MONITOR_START_DELAY_MS);
    vmon_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(vmon_ready(), "voltage-monitor shold not be ready when VOLTAGE_MONITOR_START_DELAY_MS is not elapsed after vmon_init() call");

    utest_systick_advance_time_ms(1);
    vmon_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(vmon_ready(), "voltage-monitor should be ready when VOLTAGE_MONITOR_START_DELAY_MS elapsed after vmon_init() call");
}

// Сценарий: Проверка напряжения в нормальном, низком и высоком диапазонах
// Ожидается: true для нормального напряжения, false для значений вне диапазона
static void test_vmon_check_voltage_bounds(void)
{
    LOG_INFO("Testing voltage bounds checker");

    // Нормальное напряжение
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_5V, 5000);
    bool status = vmon_check_ch_once(VMON_CHANNEL_V50);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_check_ch_once() should return true when voltage is OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V50);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_get_ch_status() should return true when voltage is OK");

    // Низкое напряжение
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_5V, 3000);
    status = vmon_check_ch_once(VMON_CHANNEL_V50);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_check_ch_once() should return false when voltage is below than FAIL min");
    status = vmon_get_ch_status(VMON_CHANNEL_V50);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_get_ch_status() should return false when voltage is below than FAIL min");

    // Нормальное напряжение
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_5V, 5000);
    status = vmon_check_ch_once(VMON_CHANNEL_V50);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_check_ch_once() should return true when voltage is OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V50);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_get_ch_status() should return true when voltage is OK");

    // Высокое напряжение
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_5V, 6500);
    status = vmon_check_ch_once(VMON_CHANNEL_V50);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_check_ch_once() should return false when voltage is above than FAIL max");
    status = vmon_get_ch_status(VMON_CHANNEL_V50);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_get_ch_status() should return false when voltage is above than FAIL max");
}

// Сценарий: Рост напряжения из OK через зону гистерезиса выше FAIL max
// Ожидается: Статус остается OK в зоне гистерезиса (между OK max и FAIL max),
// переход в FAIL при превышении FAIL max
static void test_vmon_hysteresis_ok_to_fail_max(void)
{
    LOG_INFO("Testing hysteresis: OK -> FAIL max");

    // Нормальное напряжение
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 3300);
    bool status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_check_ch_once() should return true when voltage is OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_get_ch_status() should return true when voltage is OK");

    // Напряжение между OK max и FAIL max
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 3450);
    status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "Voltage status should still be OK in the hysteresis area");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "Voltage status should still be OK in the hysteresis area");

    // Напряжение выше FAIL max
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 3600);
    status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_check_ch_once() should return false when voltage is above than FAIL max");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_get_ch_status() should return false when voltage is above than FAIL max");
}

// Сценарий: Падение напряжения из OK через зону гистерезиса ниже FAIL min
// Ожидается: Статус остается OK в зоне гистерезиса (между FAIL min и OK min),
// переход в FAIL при падении ниже FAIL min
static void test_vmon_hysteresis_ok_to_fail_min(void)
{
    LOG_INFO("Testing hysteresis: OK -> FAIL min");

    // Нормальное напряжение
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 3300);
    bool status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_check_ch_once() should return true when voltage is OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_get_ch_status() should return true when voltage is OK");

    // Напряжение между FAIL min и OK min (зона гистерезиса)
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 2850);
    status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "Voltage status should still be OK in the hysteresis area");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "Voltage status should still be OK in the hysteresis area");

    // Напряжение ниже FAIL min
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 2500);
    status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_check_ch_once() should return false when voltage is below than FAIL min");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_get_ch_status() should return false when voltage is below than FAIL min");
}

// Сценарий: Рост напряжения ниже FAIL min через зону гистерезиса до OK
// Ожидается: Статус остается FAIL в зоне гистерезиса, переход в OK при превышении
// порога OK min
static void test_vmon_hysteresis_fail_min_to_ok(void)
{
    LOG_INFO("Testing hysteresis: FAIL min -> OK");

    // Напряжение ниже FAIL min
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 2500);
    bool status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_check_ch_once() should return false when voltage is below FAIL min");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_get_ch_status() should return false when voltage is below FAIL min");

    // Напряжение между FAIL min и OK min (зона гистерезиса)
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 2850);
    status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "Voltage status should still be FAIL in the hysteresis area");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "Voltage status should still be FAIL in the hysteresis area");

    // Нормальное напряжение
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 3300);
    status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_check_ch_once() should return true when voltage is OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_get_ch_status() should return true when voltage is OK");
}

// Сценарий: Падение напряжения выше FAIL max через зону гистерезиса до OK
// Ожидается: Статус остается FAIL в зоне гистерезиса, переход в OK при снижении ниже
// порога OK max
static void test_vmon_hysteresis_fail_max_to_ok(void)
{
    LOG_INFO("Testing hysteresis: FAIL max -> OK");

    // Напряжение выше FAIL max
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 3600);
    bool status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_check_ch_once() should return false when voltage is above FAIL max");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_get_ch_status() should return false when voltage is above FAIL max");

    // Напряжение между OK max и FAIL max (зона гистерезиса)
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 3450);
    status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "Voltage status should still be FAIL in the hysteresis area");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "Voltage status should still be FAIL in the hysteresis area");

    // Нормальное напряжение
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 3300);
    status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_check_ch_once() should return true when voltage is OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_get_ch_status() should return true when voltage is OK");
}

// Сценарий: Периодическая обработка после стартовой задержки при корректных напряжениях
// Ожидается: Модуль переходит в ready, все каналы в статусе OK
static void test_vmon_do_periodic_work_after_delay(void)
{
    LOG_INFO("Testing periodic work after start delay");

    vmon_init();

    // Установка корректных напряжений
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_V_IN, 20000);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 3300);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_5V, 5000);

    // Сдвиг времени за границу стартовой задержки
    utest_systick_advance_time_ms(VOLTAGE_MONITOR_START_DELAY_MS + 10);
    vmon_do_periodic_work();

    // Проверка состояния готовности модуля
    TEST_ASSERT_TRUE_MESSAGE(vmon_ready(), "Module should be ready after start delay");

    // Проверка статуса OK для всех каналов
    bool status = vmon_get_ch_status(VMON_CHANNEL_V_IN);
    TEST_ASSERT_TRUE_MESSAGE(status, "V_IN should be OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "V33 should be OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V50);
    TEST_ASSERT_TRUE_MESSAGE(status, "V50 should be OK");
}

// Сценарий: Периодическая обработка с одним каналом вне допустимых границ
// Ожидается: Для проблемного канала статус FAIL, остальные каналы сохраняют
// независимый статус (OK)
static void test_vmon_do_periodic_work_checks_all_channels(void)
{
    LOG_INFO("Testing periodic work checks all channels");

    vmon_init();

    // Установка одного канала вне допустимых границ
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_V_IN, 20000);  // OK
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 2500);    // FAIL (ниже OK min)
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_5V, 5000);     // OK

    // Сдвиг времени и запуск периодической обработки
    utest_systick_advance_time_ms(VOLTAGE_MONITOR_START_DELAY_MS + 10);
    vmon_do_periodic_work();

    // Проверка статуса всех каналов
    bool status = vmon_get_ch_status(VMON_CHANNEL_V_IN);
    TEST_ASSERT_TRUE_MESSAGE(status, "V_IN should be OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_FALSE_MESSAGE(status, "V33 should be FAIL");
    status = vmon_get_ch_status(VMON_CHANNEL_V50);
    TEST_ASSERT_TRUE_MESSAGE(status, "V50 should be OK");
}

// Сценарий: Разные уровни напряжения на разных каналах
// Ожидается: Каждый канал возвращает свой статус (OK/FAIL) в зависимости
// от своего уровня напряжения и не зависит от других каналов
static void test_vmon_multiple_channels_independent(void)
{
    LOG_INFO("Testing multiple channels independence");

    // Установка различных напряжений для разных каналов
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_V_IN, 20000);  // OK
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_3V3, 3300);    // OK
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_5V, 3000);     // FAIL (ниже OK min)

    // Проверка каналов
    bool status = vmon_check_ch_once(VMON_CHANNEL_V_IN);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_check_ch_once() should return true for V_IN");
    status = vmon_check_ch_once(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "vmon_check_ch_once() should return true for V33");
    status = vmon_check_ch_once(VMON_CHANNEL_V50);
    TEST_ASSERT_FALSE_MESSAGE(status, "vmon_check_ch_once() should return false for V50");

    // Проверка независимости статусов
    status = vmon_get_ch_status(VMON_CHANNEL_V_IN);
    TEST_ASSERT_TRUE_MESSAGE(status, "V_IN status should be OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V33);
    TEST_ASSERT_TRUE_MESSAGE(status, "V33 status should be OK");
    status = vmon_get_ch_status(VMON_CHANNEL_V50);
    TEST_ASSERT_FALSE_MESSAGE(status, "V50 status should be FAIL");
}


int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_vmon_init);
    RUN_TEST(test_vmon_check_voltage_bounds);
    RUN_TEST(test_vmon_hysteresis_ok_to_fail_max);
    RUN_TEST(test_vmon_hysteresis_ok_to_fail_min);
    RUN_TEST(test_vmon_hysteresis_fail_min_to_ok);
    RUN_TEST(test_vmon_hysteresis_fail_max_to_ok);
    RUN_TEST(test_vmon_do_periodic_work_after_delay);
    RUN_TEST(test_vmon_do_periodic_work_checks_all_channels);
    RUN_TEST(test_vmon_multiple_channels_independent);

    return UNITY_END();
}
