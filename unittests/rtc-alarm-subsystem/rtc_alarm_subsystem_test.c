#include "unity.h"
#include "rtc.h"
#include "regmap-int.h"
#include "regmap-structs.h"
#include "irq-subsystem.h"
#include "utest_regmap.h"
#include "utest_rtc.h"
#include "utest_irq.h"
#include "rtc-alarm-subsystem.h"
#include <stdbool.h>
#include <string.h>

#define LOG_LEVEL LOG_LEVEL_INFO
#include "console_log.h"

void utest_rtc_alarm_subsystem_reset_state(void);

void setUp(void)
{
    // Сброс всех моков перед каждым тестом
    utest_regmap_reset();
    utest_rtc_reset();
    utest_irq_reset();

    // Сброс состояния модуля rtc-alarm-subsystem
    utest_rtc_alarm_subsystem_reset_state();

    // Инициализация regmap
    regmap_init();
}

void tearDown(void)
{
    // Очистка после теста
}

// ============================================================================
// Тесты чтения данных из RTC и записи в regmap (BCD -> BIN конвертация)
// ============================================================================

/**
 * Сценарий: RTC содержит время в BCD формате и готов к чтению
 * Ожидается: время копируется в regmap с конвертацией BCD->BIN, weekdays без конвертации
 */
static void test_rtc_to_regmap_time_conversion(void)
{
    LOG_INFO("Testing RTC to regmap time conversion (BCD to BIN)");

    struct rtc_time rtc_time = {
        .seconds = 0x45,
        .minutes = 0x30,
        .hours = 0x12,
        .days = 0x15,
        .weekdays = 3,
        .months = 0x06,
        .years = 0x24
    };

    utest_rtc_set_datetime(&rtc_time);
    utest_rtc_set_ready_read(true);

    rtc_alarm_do_periodic_work();

    struct REGMAP_RTC_TIME regmap_time;
    bool result = utest_regmap_get_region_data(REGMAP_REGION_RTC_TIME, &regmap_time, sizeof(regmap_time));

    TEST_ASSERT_TRUE_MESSAGE(result, "Time data should be written to regmap");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(45, regmap_time.seconds, "Seconds should be converted from BCD to BIN");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(30, regmap_time.minutes, "Minutes should be converted from BCD to BIN");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(12, regmap_time.hours, "Hours should be converted from BCD to BIN");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(15, regmap_time.days, "Days should be converted from BCD to BIN");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(3, regmap_time.weekdays, "Weekdays should not be converted");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(6, regmap_time.months, "Months should be converted from BCD to BIN");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(24, regmap_time.years, "Years should be converted from BCD to BIN");
}

/**
 * Сценарий: RTC содержит будильник в BCD формате (включен, без флага срабатывания)
 * Ожидается: будильник копируется в regmap с конвертацией BCD->BIN, флаг enabled передается
 */
static void test_rtc_to_regmap_alarm_conversion(void)
{
    LOG_INFO("Testing RTC to regmap alarm conversion (BCD to BIN)");

    struct rtc_alarm rtc_alarm = {
        .seconds = 0x30,
        .minutes = 0x45,
        .hours = 0x08,
        .days = 0x20,
        .enabled = true,
        .flag = false
    };

    utest_rtc_set_alarm(&rtc_alarm);
    utest_rtc_set_ready_read(true);

    rtc_alarm_do_periodic_work();

    struct REGMAP_RTC_ALARM regmap_alarm;
    bool result = utest_regmap_get_region_data(REGMAP_REGION_RTC_ALARM, &regmap_alarm, sizeof(regmap_alarm));

    TEST_ASSERT_TRUE_MESSAGE(result, "Alarm data should be written to regmap");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(30, regmap_alarm.seconds, "Alarm seconds should be converted from BCD to BIN");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(45, regmap_alarm.minutes, "Alarm minutes should be converted from BCD to BIN");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(8, regmap_alarm.hours, "Alarm hours should be converted from BCD to BIN");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(20, regmap_alarm.days, "Alarm days should be converted from BCD to BIN");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, regmap_alarm.en, "Alarm enabled flag should be set");
}

/**
 * Сценарий: RTC содержит значение offset и готов к чтению
 * Ожидается: offset копируется в regmap без изменений
 */
static void test_rtc_to_regmap_offset(void)
{
    LOG_INFO("Testing RTC offset copy to regmap");

    uint16_t test_offset = 0x1234;
    utest_rtc_set_offset(test_offset);
    utest_rtc_set_ready_read(true);

    rtc_alarm_do_periodic_work();

    struct REGMAP_RTC_CFG regmap_cfg;
    bool result = utest_regmap_get_region_data(REGMAP_REGION_RTC_CFG, &regmap_cfg, sizeof(regmap_cfg));

    TEST_ASSERT_TRUE_MESSAGE(result, "RTC config should be written to regmap");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(test_offset, regmap_cfg.offset, "Offset should be copied from RTC to regmap");
}

/**
 * Сценарий: RTC не готов к чтению (ready_read = false)
 * Ожидается: данные из RTC не копируются в regmap
 */
static void test_rtc_not_ready_read(void)
{
    LOG_INFO("Testing that data is not copied when RTC is not ready");

    struct rtc_time rtc_time = {
        .seconds = 0x30,
        .minutes = 0x15,
        .hours = 0x08,
        .days = 0x10,
        .weekdays = 3,
        .months = 0x05,
        .years = 0x24
    };

    utest_rtc_set_datetime(&rtc_time);
    utest_rtc_set_ready_read(false);

    rtc_alarm_do_periodic_work();

    struct REGMAP_RTC_TIME regmap_time;
    memset(&regmap_time, 0xFF, sizeof(regmap_time));
    bool result = utest_regmap_get_region_data(REGMAP_REGION_RTC_TIME, &regmap_time, sizeof(regmap_time));

    struct REGMAP_RTC_TIME expected_time;
    memset(&expected_time, 0xFF, sizeof(expected_time));

    TEST_ASSERT_FALSE_MESSAGE(result, "No data should be written to regmap when RTC is not ready");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&expected_time, &regmap_time, sizeof(regmap_time),
                                    "All fields should remain 0xFF when RTC is not ready");
}

// ============================================================================
// Тесты записи данных из regmap в RTC (BIN -> BCD конвертация)
// ============================================================================

/**
 * Сценарий: regmap содержит время в BIN формате и помечен как измененный
 * Ожидается: время записывается в RTC с конвертацией BIN->BCD, weekdays без конвертации
 */
static void test_regmap_to_rtc_time_conversion(void)
{
    LOG_INFO("Testing regmap to RTC time conversion (BIN to BCD)");

    struct REGMAP_RTC_TIME regmap_time = {
        .seconds = 59,
        .minutes = 30,
        .hours = 23,
        .days = 31,
        .weekdays = 7,
        .months = 12,
        .years = 99
    };

    regmap_set_region_data(REGMAP_REGION_RTC_TIME, &regmap_time, sizeof(regmap_time));
    utest_regmap_mark_region_changed(REGMAP_REGION_RTC_TIME);

    rtc_alarm_do_periodic_work();

    struct rtc_time rtc_time;
    bool result = utest_rtc_get_was_datetime_set(&rtc_time);

    TEST_ASSERT_TRUE_MESSAGE(result, "Time should be written to RTC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x59, rtc_time.seconds, "Seconds should be converted from BIN to BCD");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x30, rtc_time.minutes, "Minutes should be converted from BIN to BCD");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x23, rtc_time.hours, "Hours should be converted from BIN to BCD");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x31, rtc_time.days, "Days should be converted from BIN to BCD");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(7, rtc_time.weekdays, "Weekdays should not be converted");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x12, rtc_time.months, "Months should be converted from BIN to BCD");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x99, rtc_time.years, "Years should be converted from BIN to BCD");
}

/**
 * Сценарий: regmap содержит будильник в BIN формате и помечен как измененный
 * Ожидается: будильник записывается в RTC с конвертацией BIN->BCD, enabled=true, flag=false
 */
static void test_regmap_to_rtc_alarm_conversion(void)
{
    LOG_INFO("Testing regmap to RTC alarm conversion (BIN to BCD)");

    struct REGMAP_RTC_ALARM regmap_alarm = {
        .seconds = 45,
        .minutes = 30,
        .hours = 15,
        .days = 25,
        .en = 1
    };

    regmap_set_region_data(REGMAP_REGION_RTC_ALARM, &regmap_alarm, sizeof(regmap_alarm));
    utest_regmap_mark_region_changed(REGMAP_REGION_RTC_ALARM);

    rtc_alarm_do_periodic_work();

    struct rtc_alarm rtc_alarm;
    bool result = utest_rtc_get_was_alarm_set(&rtc_alarm);

    TEST_ASSERT_TRUE_MESSAGE(result, "Alarm should be written to RTC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x45, rtc_alarm.seconds, "Alarm seconds should be converted from BIN to BCD");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x30, rtc_alarm.minutes, "Alarm minutes should be converted from BIN to BCD");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x15, rtc_alarm.hours, "Alarm hours should be converted from BIN to BCD");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x25, rtc_alarm.days, "Alarm days should be converted from BIN to BCD");
    TEST_ASSERT_TRUE_MESSAGE(rtc_alarm.enabled, "Alarm should be enabled");
    TEST_ASSERT_FALSE_MESSAGE(rtc_alarm.flag, "Alarm flag should be cleared when writing");
}

/**
 * Сценарий: regmap содержит значение offset и помечен как измененный
 * Ожидается: offset записывается в RTC без изменений
 */
static void test_regmap_to_rtc_offset(void)
{
    LOG_INFO("Testing regmap offset copy to RTC");

    struct REGMAP_RTC_CFG regmap_cfg = {
        .offset = 0xABCD
    };

    regmap_set_region_data(REGMAP_REGION_RTC_CFG, &regmap_cfg, sizeof(regmap_cfg));
    utest_regmap_mark_region_changed(REGMAP_REGION_RTC_CFG);

    rtc_alarm_do_periodic_work();

    uint16_t rtc_offset;
    bool result = utest_rtc_get_was_offset_set(&rtc_offset);

    TEST_ASSERT_TRUE_MESSAGE(result, "Offset should be written to RTC");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0xABCD, rtc_offset, "Offset should be copied from regmap to RTC");
}

/**
 * Сценарий: regmap содержит данные, но регионы не помечены как измененные
 * Ожидается: данные не записываются в RTC (ни время, ни будильник, ни offset)
 */
static void test_regmap_not_changed(void)
{
    LOG_INFO("Testing that RTC is not updated when regmap data hasn't changed");

    struct REGMAP_RTC_TIME regmap_time = {
        .seconds = 30,
        .minutes = 15,
        .hours = 10,
        .days = 20,
        .weekdays = 5,
        .months = 8,
        .years = 23
    };
    regmap_set_region_data(REGMAP_REGION_RTC_TIME, &regmap_time, sizeof(regmap_time));

    struct REGMAP_RTC_ALARM regmap_alarm = {
        .seconds = 45,
        .minutes = 30,
        .hours = 8,
        .days = 15,
        .en = 1
    };
    regmap_set_region_data(REGMAP_REGION_RTC_ALARM, &regmap_alarm, sizeof(regmap_alarm));

    struct REGMAP_RTC_CFG regmap_cfg = {
        .offset = 0x5678
    };
    regmap_set_region_data(REGMAP_REGION_RTC_CFG, &regmap_cfg, sizeof(regmap_cfg));

    rtc_alarm_do_periodic_work();

    struct rtc_time rtc_time;
    bool result_time = utest_rtc_get_was_datetime_set(&rtc_time);
    TEST_ASSERT_FALSE_MESSAGE(result_time,
                             "RTC time should not be updated when regmap data hasn't changed");

    struct rtc_alarm rtc_alarm;
    bool result_alarm = utest_rtc_get_was_alarm_set(&rtc_alarm);
    TEST_ASSERT_FALSE_MESSAGE(result_alarm,
                             "RTC alarm should not be updated when regmap data hasn't changed");

    uint16_t rtc_offset;
    bool result_offset = utest_rtc_get_was_offset_set(&rtc_offset);
    TEST_ASSERT_FALSE_MESSAGE(result_offset,
                             "RTC offset should not be updated when regmap data hasn't changed");
}

/**
 * Сценарий: все три региона (RTC_TIME, RTC_ALARM, RTC_CFG) помечены как измененные одновременно
 * Ожидается: все три региона записываются в RTC с корректной конвертацией
 */
static void test_all_regions_changed_simultaneously(void)
{
    LOG_INFO("Testing simultaneous change of all three regmap regions");

    struct REGMAP_RTC_TIME regmap_time = {
        .seconds = 45,
        .minutes = 30,
        .hours = 14,
        .days = 25,
        .weekdays = 5,
        .months = 11,
        .years = 25
    };
    regmap_set_region_data(REGMAP_REGION_RTC_TIME, &regmap_time, sizeof(regmap_time));
    utest_regmap_mark_region_changed(REGMAP_REGION_RTC_TIME);

    struct REGMAP_RTC_ALARM regmap_alarm = {
        .seconds = 0,
        .minutes = 0,
        .hours = 6,
        .days = 1,
        .en = 1
    };
    regmap_set_region_data(REGMAP_REGION_RTC_ALARM, &regmap_alarm, sizeof(regmap_alarm));
    utest_regmap_mark_region_changed(REGMAP_REGION_RTC_ALARM);

    struct REGMAP_RTC_CFG regmap_cfg = {
        .offset = 0x1234
    };
    regmap_set_region_data(REGMAP_REGION_RTC_CFG, &regmap_cfg, sizeof(regmap_cfg));
    utest_regmap_mark_region_changed(REGMAP_REGION_RTC_CFG);

    rtc_alarm_do_periodic_work();

    struct rtc_time rtc_time;
    bool result_time = utest_rtc_get_was_datetime_set(&rtc_time);
    TEST_ASSERT_TRUE_MESSAGE(result_time, "RTC time should be updated");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x45, rtc_time.seconds, "Seconds should be converted to BCD");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x30, rtc_time.minutes, "Minutes should be converted to BCD");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x14, rtc_time.hours, "Hours should be converted to BCD");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x25, rtc_time.days, "Days should be converted to BCD");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(5, rtc_time.weekdays, "Weekdays should not be converted");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x11, rtc_time.months, "Months should be converted to BCD");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x25, rtc_time.years, "Years should be converted to BCD");

    struct rtc_alarm rtc_alarm;
    bool result_alarm = utest_rtc_get_was_alarm_set(&rtc_alarm);
    TEST_ASSERT_TRUE_MESSAGE(result_alarm, "RTC alarm should be updated");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, rtc_alarm.seconds, "Alarm seconds should be converted to BCD");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, rtc_alarm.minutes, "Alarm minutes should be converted to BCD");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x06, rtc_alarm.hours, "Alarm hours should be converted to BCD");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, rtc_alarm.days, "Alarm days should be converted to BCD");
    TEST_ASSERT_TRUE_MESSAGE(rtc_alarm.enabled, "Alarm should be enabled");
    TEST_ASSERT_FALSE_MESSAGE(rtc_alarm.flag, "Alarm flag should be cleared");

    uint16_t rtc_offset;
    bool result_offset = utest_rtc_get_was_offset_set(&rtc_offset);
    TEST_ASSERT_TRUE_MESSAGE(result_offset, "RTC offset should be updated");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x1234, rtc_offset, "Offset should be copied to RTC");
}

// ============================================================================
// Тесты обработки срабатывания будильника
// ============================================================================

/**
 * Сценарий: RTC содержит будильник с установленным флагом срабатывания
 * Ожидается:
 *   - устанавливается IRQ_ALARM флаг
 *   - очищается флаг будильника в RTC
 *   - будильник автоматически отключается (чтобы предотвратить повторное срабатывание)
 */
static void test_alarm_trigger_behavior(void)
{
    LOG_INFO("Testing alarm trigger: IRQ set, flag cleared, alarm disabled");

    struct rtc_alarm rtc_alarm = {
        .seconds = 0x30,
        .minutes = 0x15,
        .hours = 0x08,
        .days = 0x05,
        .enabled = true,
        .flag = true
    };

    utest_rtc_set_alarm(&rtc_alarm);
    utest_rtc_set_ready_read(true);

    rtc_alarm_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_irq_is_flag_set(IRQ_ALARM),
                            "IRQ_ALARM flag should be set when alarm triggers");

    TEST_ASSERT_TRUE_MESSAGE(utest_rtc_was_alarm_flag_cleared(),
                            "Alarm flag should be cleared after trigger");

    struct rtc_alarm rtc_alarm_set;
    bool result = utest_rtc_get_was_alarm_set(&rtc_alarm_set);
    TEST_ASSERT_TRUE_MESSAGE(result, "Alarm should be written to RTC after trigger");
    TEST_ASSERT_FALSE_MESSAGE(rtc_alarm_set.enabled,
                             "Alarm should be disabled after trigger to prevent recurring triggers");
}

/**
 * Сценарий: RTC содержит включенный будильник без флага срабатывания
 * Ожидается: IRQ_ALARM флаг не устанавливается
 */
static void test_alarm_no_flag_no_irq(void)
{
    LOG_INFO("Testing that IRQ is not set when alarm flag is not active");

    struct rtc_alarm rtc_alarm = {
        .seconds = 0,
        .minutes = 0,
        .hours = 0,
        .days = 1,
        .enabled = true,
        .flag = false
    };

    utest_rtc_set_alarm(&rtc_alarm);
    utest_rtc_set_ready_read(true);

    rtc_alarm_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(utest_irq_is_flag_set(IRQ_ALARM),
                             "IRQ_ALARM flag should not be set when alarm hasn't triggered");
}

// ============================================================================
// Тесты функции rtc_alarm_is_alarm_enabled
// ============================================================================

/**
 * Сценарий: проверка значения rtc_alarm_is_alarm_enabled() без предшествующего вызова periodic_work
 * Ожидается: возвращает false (static переменная alarm_enabled инициализируется как 0)
 */
static void test_alarm_enabled_initial_state(void)
{
    LOG_INFO("Testing rtc_alarm_is_alarm_enabled in initial state");

    TEST_ASSERT_FALSE_MESSAGE(rtc_alarm_is_alarm_enabled(),
                             "rtc_alarm_is_alarm_enabled should return false before first periodic_work call");
}

/**
 * Сценарий: RTC содержит включенный будильник (enabled=true)
 * Ожидается: rtc_alarm_is_alarm_enabled() возвращает true
 */
static void test_alarm_enabled_status_true(void)
{
    LOG_INFO("Testing rtc_alarm_is_alarm_enabled returns true when alarm is enabled");

    struct rtc_alarm rtc_alarm = {
        .seconds = 0,
        .minutes = 0,
        .hours = 0,
        .days = 1,
        .enabled = true,
        .flag = false
    };

    utest_rtc_set_alarm(&rtc_alarm);
    utest_rtc_set_ready_read(true);

    rtc_alarm_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(rtc_alarm_is_alarm_enabled(),
                            "rtc_alarm_is_alarm_enabled should return true when alarm is enabled");
}

/**
 * Сценарий: RTC содержит отключенный будильник (enabled=false)
 * Ожидается: rtc_alarm_is_alarm_enabled() возвращает false
 */
static void test_alarm_enabled_status_false(void)
{
    LOG_INFO("Testing rtc_alarm_is_alarm_enabled returns false when alarm is disabled");

    struct rtc_alarm rtc_alarm = {
        .seconds = 0,
        .minutes = 0,
        .hours = 0,
        .days = 1,
        .enabled = false,
        .flag = false
    };

    utest_rtc_set_alarm(&rtc_alarm);
    utest_rtc_set_ready_read(true);

    rtc_alarm_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(rtc_alarm_is_alarm_enabled(),
                             "rtc_alarm_is_alarm_enabled should return false when alarm is disabled");
}

/**
 * Сценарий: будильник срабатывает (флаг установлен)
 * Ожидается: после срабатывания rtc_alarm_is_alarm_enabled() возвращает false
 */
static void test_alarm_enabled_status_after_trigger(void)
{
    LOG_INFO("Testing rtc_alarm_is_alarm_enabled returns false after alarm triggers");

    struct rtc_alarm rtc_alarm = {
        .seconds = 0,
        .minutes = 0,
        .hours = 0,
        .days = 1,
        .enabled = true,
        .flag = true
    };

    utest_rtc_set_alarm(&rtc_alarm);
    utest_rtc_set_ready_read(true);

    rtc_alarm_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(rtc_alarm_is_alarm_enabled(),
                             "rtc_alarm_is_alarm_enabled should return false after alarm triggers");
}

// ============================================================================
// Комплексные тесты
// ============================================================================

/**
 * Сценарий: полный цикл RTC->regmap->RTC
 *   1. RTC готов, данные копируются в regmap с конвертацией BCD->BIN
 *   2. Данные в regmap изменяются и помечаются как changed
 *   3. RTC помечается как не готовый (чтобы избежать перезаписи)
 * Ожидается: измененные данные записываются в RTC с конвертацией BIN->BCD
 */
static void test_full_cycle_rtc_to_regmap_and_back(void)
{
    LOG_INFO("Testing full cycle: RTC -> regmap -> RTC");

    struct rtc_time rtc_time_initial = {
        .seconds = 0x30,
        .minutes = 0x45,
        .hours = 0x12,
        .days = 0x15,
        .weekdays = 3,
        .months = 0x06,
        .years = 0x24
    };

    utest_rtc_set_datetime(&rtc_time_initial);
    utest_rtc_set_ready_read(true);
    rtc_alarm_do_periodic_work();

    struct REGMAP_RTC_TIME regmap_time;
    utest_regmap_get_region_data(REGMAP_REGION_RTC_TIME, &regmap_time, sizeof(regmap_time));
    TEST_ASSERT_EQUAL_UINT8(30, regmap_time.seconds);
    TEST_ASSERT_EQUAL_UINT8(45, regmap_time.minutes);

    utest_rtc_set_ready_read(false);

    regmap_time.seconds = 59;
    regmap_time.minutes = 15;
    regmap_set_region_data(REGMAP_REGION_RTC_TIME, &regmap_time, sizeof(regmap_time));
    utest_regmap_mark_region_changed(REGMAP_REGION_RTC_TIME);

    rtc_alarm_do_periodic_work();

    struct rtc_time rtc_time_updated;
    bool result = utest_rtc_get_was_datetime_set(&rtc_time_updated);

    TEST_ASSERT_TRUE_MESSAGE(result, "DateTime should be written to RTC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x59, rtc_time_updated.seconds, "Seconds should be 0x59 in BCD");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x15, rtc_time_updated.minutes, "Minutes should be 0x15 in BCD");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x12, rtc_time_updated.hours, "Hours should stay 0x12");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x15, rtc_time_updated.days, "Days should stay 0x15");
}

/**
 * Сценарий: многократный вызов rtc_alarm_do_periodic_work() с одними и теми же данными
 * Ожидается: данные корректно копируются из RTC в regmap при каждом вызове
 */
static void test_multiple_periodic_work_calls(void)
{
    LOG_INFO("Testing multiple calls to rtc_alarm_do_periodic_work");

    struct rtc_time rtc_time = {
        .seconds = 0x47,
        .minutes = 0x23,
        .hours = 0x14,
        .days = 0x18,
        .weekdays = 4,
        .months = 0x09,
        .years = 0x25
    };

    utest_rtc_set_datetime(&rtc_time);
    utest_rtc_set_ready_read(true);

    for (int i = 0; i < 5; i++) {
        rtc_alarm_do_periodic_work();
    }

    struct REGMAP_RTC_TIME regmap_time;
    utest_regmap_get_region_data(REGMAP_REGION_RTC_TIME, &regmap_time, sizeof(regmap_time));

    TEST_ASSERT_EQUAL_UINT8(47, regmap_time.seconds);
    TEST_ASSERT_EQUAL_UINT8(23, regmap_time.minutes);
    TEST_ASSERT_EQUAL_UINT8(14, regmap_time.hours);
    TEST_ASSERT_EQUAL_UINT8(18, regmap_time.days);
    TEST_ASSERT_EQUAL_UINT8(4, regmap_time.weekdays);
    TEST_ASSERT_EQUAL_UINT8(9, regmap_time.months);
    TEST_ASSERT_EQUAL_UINT16(25, regmap_time.years);
}

// ============================================================================
// Main function
// ============================================================================

int main(void)
{
    UNITY_BEGIN();

    // Тесты конвертации RTC -> regmap
    RUN_TEST(test_rtc_to_regmap_time_conversion);
    RUN_TEST(test_rtc_to_regmap_alarm_conversion);
    RUN_TEST(test_rtc_to_regmap_offset);
    RUN_TEST(test_rtc_not_ready_read);

    // Тесты конвертации regmap -> RTC
    RUN_TEST(test_regmap_to_rtc_time_conversion);
    RUN_TEST(test_regmap_to_rtc_alarm_conversion);
    RUN_TEST(test_regmap_to_rtc_offset);
    RUN_TEST(test_regmap_not_changed);
    RUN_TEST(test_all_regions_changed_simultaneously);

    // Тесты срабатывания будильника
    RUN_TEST(test_alarm_trigger_behavior);
    RUN_TEST(test_alarm_no_flag_no_irq);

    // Тесты статуса будильника
    RUN_TEST(test_alarm_enabled_initial_state);
    RUN_TEST(test_alarm_enabled_status_true);
    RUN_TEST(test_alarm_enabled_status_false);
    RUN_TEST(test_alarm_enabled_status_after_trigger);

    // Комплексные тесты
    RUN_TEST(test_full_cycle_rtc_to_regmap_and_back);
    RUN_TEST(test_multiple_periodic_work_calls);

    return UNITY_END();
}
