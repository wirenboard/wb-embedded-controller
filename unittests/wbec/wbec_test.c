#include "unity.h"
#include "wbec.h"
#include "config.h"
#include "mcu-pwr.h"
#include "voltage-monitor.h"
#include "wbec_test_stubs.h"
#include "utest_mcu_pwr.h"
#include "utest_systick.h"
#include "utest_voltage_monitor.h"
#include "utest_regmap.h"
#include "utest_system_led.h"
#include "utest_wbmz_common.h"
#include "utest_wbmcu_system.h"
#include "utest_wdt_stm32.h"
#include "regmap-int.h"

#define LOG_LEVEL LOG_LEVEL_INFO
#include "console_log.h"

void utest_wbec_reset_state(void);

static void reset_all(void)
{
    utest_wbec_reset_state();
    utest_mcu_reset();
    utest_systick_set_time_ms(1000);
    utest_vmon_reset();
    utest_regmap_reset();
    utest_system_led_reset();
    utest_wbmz_reset();
    utest_pwr_reset();
    utest_watchdog_reset();
    utest_linux_pwr_reset();
    utest_pwrkey_reset();
    utest_wdt_reset();
    utest_temp_ctrl_reset();
    utest_rtc_reset();
    utest_rtc_alarm_reset();
    utest_irq_reset();
}

void setUp(void)
{
    reset_all();
}

void tearDown(void)
{
}

// Вспомогательная функция: прочитать poweron_reason из regmap-а
// Для этого вызываем wbec_do_periodic_work (при невключённом vmon - состояние не меняется)
// и читаем данные из regmap
static uint16_t get_poweron_reason_from_regmap(void)
{
    wbec_do_periodic_work();
    struct REGMAP_POWERON_REASON pr;
    TEST_ASSERT_TRUE(utest_regmap_get_region_data(REGMAP_REGION_POWERON_REASON, &pr, sizeof(pr)));
    return pr.poweron_reason;
}

// ======================== wbec_init: POWER_ON ========================

// Сценарий: штатное включение от внешнего источника питания (POWER_ON) при наличии +5В.
// Ожидание: установлена причина REASON_POWER_ON, периодическое пробуждение отключено,
// светодиод мигает в режиме ожидания запуска, WBMZ stepup не включён.
static void test_init_power_on_with_5v(void)
{
    LOG_INFO("Power-on reason POWER_ON with 5V present: normal startup path");

    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);

    wbec_init();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, get_poweron_reason_from_regmap(),
                                     "poweron_reason must be REASON_POWER_ON (0)");
    TEST_ASSERT_TRUE_MESSAGE(utest_rtc_get_periodic_wakeup_disabled(),
                             "RTC periodic wakeup must be disabled after init");
    TEST_ASSERT_EQUAL_MESSAGE(UTEST_LED_MODE_BLINK, utest_system_led_get_mode(),
                              "LED must blink in WAIT_STARTUP state");
    TEST_ASSERT_FALSE_MESSAGE(utest_wbmz_get_stepup_enabled(),
                              "WBMZ stepup must NOT be enabled when 5V is present");
    TEST_ASSERT_FALSE_MESSAGE(utest_linux_pwr_get_standby_called(),
                              "Standby must NOT be called on normal power-on");
}

// Сценарий: включение от питания, но +5В отсутствует (питание от WBMZ).
// Ожидание: включается WBMZ stepup для поддержания питания МК.
static void test_init_power_on_without_5v_enables_stepup(void)
{
    LOG_INFO("Power-on reason POWER_ON without 5V: WBMZ stepup must be enabled");

    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, false);

    wbec_init();

    TEST_ASSERT_TRUE_MESSAGE(utest_wbmz_get_stepup_enabled(),
                             "WBMZ stepup must be enabled when 5V is absent");
    TEST_ASSERT_TRUE_MESSAGE(utest_rtc_get_periodic_wakeup_disabled(),
                             "RTC periodic wakeup must be disabled after init");
}

// Сценарий: неизвестная причина включения обрабатывается как POWER_ON.
// Ожидание: поведение идентично обычному включению от питания.
static void test_init_unknown_reason_defaults_to_power_on(void)
{
    LOG_INFO("Unknown power-on reason defaults to REASON_POWER_ON");

    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_UNKNOWN);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);

    wbec_init();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, get_poweron_reason_from_regmap(),
                                     "poweron_reason must be REASON_POWER_ON (0) for unknown MCU reason");
    TEST_ASSERT_TRUE_MESSAGE(utest_rtc_get_periodic_wakeup_disabled(),
                             "RTC periodic wakeup must be disabled after init");
}

// ======================== wbec_init: POWER_KEY ========================

// Сценарий: включение от кнопки, кнопка нажата (прошла антидребезг).
// Ожидание: причина REASON_POWER_KEY, штатный запуск.
static void test_init_power_key_pressed(void)
{
    LOG_INFO("Power-on by power key: button pressed and debounced");

    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_KEY);
    utest_pwrkey_set_pressed(true);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);

    wbec_init();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, get_poweron_reason_from_regmap(),
                                     "poweron_reason must be REASON_POWER_KEY (1)");
    TEST_ASSERT_TRUE_MESSAGE(utest_rtc_get_periodic_wakeup_disabled(),
                             "RTC periodic wakeup must be disabled after init");
    TEST_ASSERT_FALSE_MESSAGE(utest_linux_pwr_get_standby_called(),
                              "Standby must NOT be called when button is pressed");
}

// Сценарий: включение от кнопки, но кнопка не нажата (ложное срабатывание).
// Ожидание: МК уходит в standby через linux_cpu_pwr_seq_off_and_goto_standby,
// т.к. это был короткий дребезг без реального нажатия.
static void test_init_power_key_not_pressed_goes_to_standby(void)
{
    LOG_INFO("Power-on by power key: button NOT pressed goes to standby");

    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_KEY);
    utest_pwrkey_set_pressed(false);

    jmp_buf standby_jmp;
    utest_linux_pwr_set_standby_exit_jmp(&standby_jmp);

    if (setjmp(standby_jmp) == 0) {
        wbec_init();
        // Если мы дошли сюда - standby не был вызван, это ошибка
        TEST_FAIL_MESSAGE("wbec_init must call linux_cpu_pwr_seq_off_and_goto_standby when button is not pressed");
    }

    // Прыжок из standby-заглушки - проверяем, что standby был вызван
    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_standby_called(),
                             "Standby must be called when power key is not pressed");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(WBEC_PERIODIC_WAKEUP_NEXT_TIMEOUT_S,
                                     utest_linux_pwr_get_standby_wakeup_s(),
                                     "Standby wakeup timeout must match WBEC_PERIODIC_WAKEUP_NEXT_TIMEOUT_S");
}

// ======================== wbec_init: RTC_ALARM ========================

// Сценарий: включение по будильнику RTC.
// Ожидание: причина REASON_RTC_ALARM, штатный запуск.
static void test_init_rtc_alarm(void)
{
    LOG_INFO("Power-on by RTC alarm: normal startup with alarm reason");

    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_RTC_ALARM);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);

    wbec_init();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(2, get_poweron_reason_from_regmap(),
                                     "poweron_reason must be REASON_RTC_ALARM (2)");
    TEST_ASSERT_TRUE_MESSAGE(utest_rtc_get_periodic_wakeup_disabled(),
                             "RTC periodic wakeup must be disabled after init");
}

// ======================== wbec_init: RTC_PERIODIC_WAKEUP ========================

// Сценарий: периодическое пробуждение, +5В появилось (было выключено, стало включено).
// Ожидание: МК включается в штатном режиме, причина REASON_POWER_ON.
static void test_init_periodic_wakeup_5v_appeared(void)
{
    LOG_INFO("Periodic wakeup: 5V appeared (was off -> on) starts normal boot");

    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_RTC_PERIODIC_WAKEUP);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);
    utest_vmon_set_ch_status(VMON_CHANNEL_V33, false);
    utest_mcu_set_vcc_5v_state(MCU_VCC_5V_STATE_OFF);

    wbec_init();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, get_poweron_reason_from_regmap(),
                                     "poweron_reason must be REASON_POWER_ON (0) when 5V appeared");
    TEST_ASSERT_TRUE_MESSAGE(utest_rtc_get_periodic_wakeup_disabled(),
                             "RTC periodic wakeup must be disabled after init");
    TEST_ASSERT_FALSE_MESSAGE(utest_linux_pwr_get_standby_called(),
                              "Standby must NOT be called when 5V appeared");
}

// Сценарий: периодическое пробуждение, +5В есть и +3.3В тоже есть.
// Это может означать, что МК перезагрузился (например, EC RESET) во время работы.
// Ожидание: штатный запуск, причина REASON_POWER_ON.
static void test_init_periodic_wakeup_3v3_present_powers_on(void)
{
    LOG_INFO("Periodic wakeup: 3.3V presence means system already running, boot normally");

    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_RTC_PERIODIC_WAKEUP);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);
    utest_vmon_set_ch_status(VMON_CHANNEL_V33, true);
    utest_mcu_set_vcc_5v_state(MCU_VCC_5V_STATE_ON);

    wbec_init();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, get_poweron_reason_from_regmap(),
                                     "poweron_reason must be REASON_POWER_ON when 3.3V is present");
    TEST_ASSERT_FALSE_MESSAGE(utest_linux_pwr_get_standby_called(),
                              "Standby must NOT be called when 3.3V is present");
}

// Сценарий: периодическое пробуждение, +5В есть, но было уже включено и нет +3.3В.
// Ожидание: МК уходит в standby (питание не появилось, а было и раньше).
static void test_init_periodic_wakeup_5v_was_on_no_3v3_goes_to_standby(void)
{
    LOG_INFO("Periodic wakeup: 5V was already on, no 3.3V -> back to standby");

    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_RTC_PERIODIC_WAKEUP);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);
    utest_vmon_set_ch_status(VMON_CHANNEL_V33, false);
    utest_mcu_set_vcc_5v_state(MCU_VCC_5V_STATE_ON);

    jmp_buf standby_jmp;
    utest_linux_pwr_set_standby_exit_jmp(&standby_jmp);

    if (setjmp(standby_jmp) == 0) {
        wbec_init();
        TEST_FAIL_MESSAGE("wbec_init must go to standby when 5V was already on");
    }

    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_standby_called(),
                             "Standby must be called when 5V was already on and no 3.3V");
}

// Сценарий: периодическое пробуждение, +5В пропало (было включено).
// Ожидание: сохраняется состояние VCC_5V_STATE_OFF и МК уходит в standby.
static void test_init_periodic_wakeup_no_5v_was_on_saves_state(void)
{
    LOG_INFO("Periodic wakeup: 5V lost (was on) saves OFF state and goes to standby");

    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_RTC_PERIODIC_WAKEUP);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, false);
    utest_mcu_set_vcc_5v_state(MCU_VCC_5V_STATE_ON);

    jmp_buf standby_jmp;
    utest_linux_pwr_set_standby_exit_jmp(&standby_jmp);

    if (setjmp(standby_jmp) == 0) {
        wbec_init();
        TEST_FAIL_MESSAGE("wbec_init must go to standby when 5V is absent");
    }

    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_standby_called(),
                             "Standby must be called when 5V is lost");
    TEST_ASSERT_EQUAL_MESSAGE(MCU_VCC_5V_STATE_OFF, mcu_get_vcc_5v_last_state(),
                              "VCC 5V last state must be saved as OFF when 5V disappears");
}

// Сценарий: периодическое пробуждение, +5В не было и нет (было выключено).
// Ожидание: МК уходит в standby; состояние не меняется (и так OFF).
static void test_init_periodic_wakeup_no_5v_was_off_goes_to_standby(void)
{
    LOG_INFO("Periodic wakeup: 5V absent (was already off) goes back to standby");

    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_RTC_PERIODIC_WAKEUP);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, false);
    utest_mcu_set_vcc_5v_state(MCU_VCC_5V_STATE_OFF);

    jmp_buf standby_jmp;
    utest_linux_pwr_set_standby_exit_jmp(&standby_jmp);

    if (setjmp(standby_jmp) == 0) {
        wbec_init();
        TEST_FAIL_MESSAGE("wbec_init must go to standby when 5V is absent");
    }

    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_standby_called(),
                             "Standby must be called when 5V is absent");
    TEST_ASSERT_EQUAL_MESSAGE(MCU_VCC_5V_STATE_OFF, mcu_get_vcc_5v_last_state(),
                              "VCC 5V last state must remain OFF");
}

// Сценарий: периодическое пробуждение, +5В пропало, но при этом не было включено.
// Ожидание: МК не сохраняет состояние заново (и так OFF) и уходит в standby.
// Отличие от предыдущего теста: проверяем что не происходит лишнего save.
static void test_init_periodic_wakeup_no_5v_was_off_no_extra_save(void)
{
    LOG_INFO("Periodic wakeup: 5V absent and was already off, no state change");

    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_RTC_PERIODIC_WAKEUP);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, false);
    utest_mcu_set_vcc_5v_state(MCU_VCC_5V_STATE_OFF);

    jmp_buf standby_jmp;
    utest_linux_pwr_set_standby_exit_jmp(&standby_jmp);

    if (setjmp(standby_jmp) == 0) {
        wbec_init();
        TEST_FAIL_MESSAGE("wbec_init must go to standby when 5V is absent");
    }

    // Проверяем, что состояние осталось OFF (не было перезаписано)
    TEST_ASSERT_EQUAL_MESSAGE(MCU_VCC_5V_STATE_OFF, mcu_get_vcc_5v_last_state(),
                              "VCC 5V state must stay OFF without rewrite");
}

// ======================== wbec_init: общие проверки ========================

// Сценарий: при отсутствии +5В после любого штатного включения должен включаться wbmz stepup.
// Проверяем для разных причин включения.
static void test_init_enables_stepup_when_no_5v(void)
{
    LOG_INFO("Any successful init without 5V must enable WBMZ stepup");

    // Проверяем для RTC_ALARM
    reset_all();
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_RTC_ALARM);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, false);

    wbec_init();

    TEST_ASSERT_TRUE_MESSAGE(utest_wbmz_get_stepup_enabled(),
                             "WBMZ stepup must be enabled when 5V absent (RTC_ALARM)");

    // Проверяем для POWER_KEY
    reset_all();
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_KEY);
    utest_pwrkey_set_pressed(true);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, false);

    wbec_init();

    TEST_ASSERT_TRUE_MESSAGE(utest_wbmz_get_stepup_enabled(),
                             "WBMZ stepup must be enabled when 5V absent (POWER_KEY)");
}

// Сценарий: при наличии +5В stepup не включается.
static void test_init_does_not_enable_stepup_when_5v_present(void)
{
    LOG_INFO("Init with 5V present must not enable WBMZ stepup");

    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);

    wbec_init();

    TEST_ASSERT_FALSE_MESSAGE(utest_wbmz_get_stepup_enabled(),
                              "WBMZ stepup must not be enabled when 5V is present");
}

int main(void)
{
    UNITY_BEGIN();

    // Штатное включение от питания
    RUN_TEST(test_init_power_on_with_5v);
    RUN_TEST(test_init_power_on_without_5v_enables_stepup);
    RUN_TEST(test_init_unknown_reason_defaults_to_power_on);

    // Включение по кнопке
    RUN_TEST(test_init_power_key_pressed);
    RUN_TEST(test_init_power_key_not_pressed_goes_to_standby);

    // Включение по будильнику RTC
    RUN_TEST(test_init_rtc_alarm);

    // Периодическое пробуждение RTC
    RUN_TEST(test_init_periodic_wakeup_5v_appeared);
    RUN_TEST(test_init_periodic_wakeup_3v3_present_powers_on);
    RUN_TEST(test_init_periodic_wakeup_5v_was_on_no_3v3_goes_to_standby);
    RUN_TEST(test_init_periodic_wakeup_no_5v_was_on_saves_state);
    RUN_TEST(test_init_periodic_wakeup_no_5v_was_off_goes_to_standby);
    RUN_TEST(test_init_periodic_wakeup_no_5v_was_off_no_extra_save);

    // Общие проверки
    RUN_TEST(test_init_enables_stepup_when_no_5v);
    RUN_TEST(test_init_does_not_enable_stepup_when_5v_present);

    return UNITY_END();
}
