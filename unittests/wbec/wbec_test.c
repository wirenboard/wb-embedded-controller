#include "unity.h"
#include "wbec.h"
#include "adc.h"
#include "config.h"
#include "mcu-pwr.h"
#include "voltage-monitor.h"
#include "wbec_test_stubs.h"
#include "utest_adc.h"
#include "utest_mcu_pwr.h"
#include "utest_systick.h"
#include "utest_voltage_monitor.h"
#include "utest_irq.h"
#include "utest_regmap.h"
#include "utest_rtc.h"
#include "utest_system_led.h"
#include "utest_wbmz_common.h"
#include "utest_wbmcu_system.h"
#include "utest_wdt_stm32.h"
#include "utest_pwrkey.h"
#include "regmap-int.h"

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
    utest_temp_set_ready(true);
    utest_pwrkey_reset();
    utest_wdt_reset();
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

// Вспомогательная функция: прочитать ADC_DATA из regmap-а.
// При каждом вызове periodic_work данные АЦП обновляются вне зависимости от состояния автомата.
static struct REGMAP_ADC_DATA get_adc_data_from_regmap(void)
{
    wbec_do_periodic_work();
    struct REGMAP_ADC_DATA adc_data;
    TEST_ASSERT_TRUE(utest_regmap_get_region_data(REGMAP_REGION_ADC_DATA, &adc_data, sizeof(adc_data)));
    return adc_data;
}

// ======================== wbec_init: POWER_ON ========================

// Сценарий: штатное включение от внешнего источника питания (POWER_ON) при наличии +5В.
// Ожидание: установлена причина REASON_POWER_ON, периодическое пробуждение отключено,
// светодиод мигает в режиме ожидания запуска, WBMZ stepup не включён.
static void test_init_power_on_with_5v(void)
{
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
// Ожидание: МК уходит в standby, а сохранённое состояние VCC 5В остаётся OFF
// Отличие от предыдущего теста: здесь дополнительно проверяется сохранённое состояние VCC 5В
static void test_init_periodic_wakeup_no_5v_was_off_no_extra_save(void)
{
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
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);

    wbec_init();

    TEST_ASSERT_FALSE_MESSAGE(utest_wbmz_get_stepup_enabled(),
                              "WBMZ stepup must not be enabled when 5V is present");
}

// ======================== wbec_init: покрытие цикла pwrkey_ready ========================

static void pwrkey_ready_callback(void)
{
    utest_pwrkey_set_ready(true);
}

// Сценарий: включение от кнопки, pwrkey не сразу готов (антидребезг ещё не завершён).
// Ожидание: цикл while(!pwrkey_ready()) вызывает pwrkey_do_periodic_work и watchdog_reload,
// после чего кнопка нажата → штатное включение.
static void test_init_power_key_waits_for_pwrkey_ready(void)
{
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_KEY);
    utest_pwrkey_set_ready(false);
    utest_pwrkey_set_pressed(true);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);
    utest_watchdog_set_reload_callback(pwrkey_ready_callback);

    wbec_init();

    TEST_ASSERT_TRUE_MESSAGE(utest_watchdog_is_reloaded(),
                             "Watchdog must be reloaded while waiting for pwrkey ready");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, get_poweron_reason_from_regmap(),
                                     "poweron_reason must be REASON_POWER_KEY (1) after debounce wait");
}

// ======================== wbec_do_periodic_work: вспомогательные функции ========================

// Вспомогательная: инициализация и переход в WORKING через обычный путь загрузки
// (без V33 при старте, через VOLTAGE_CHECK)
static void drive_to_working_state(void)
{
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);
    wbec_init();

    // WAIT_STARTUP → VOLTAGE_CHECK
    utest_vmon_set_ready(true);
    wbec_do_periodic_work();

    // VOLTAGE_CHECK → POWER_ON_SEQUENCE_WAIT (temp_ready по умолчанию)
    wbec_do_periodic_work();

    // POWER_ON_SEQUENCE_WAIT → WORKING (is_busy=false по умолчанию)
    utest_vmon_set_ch_status(VMON_CHANNEL_V33, true);
    wbec_do_periodic_work();
}

// Вспомогательная: запись POWER_CTRL в regmap и пометка региона как изменённого
static void set_power_ctrl_request(bool off, bool reboot, bool reset_pmic)
{
    struct REGMAP_POWER_CTRL p = {
        .off = off ? 1 : 0,
        .reboot = reboot ? 1 : 0,
        .reset_pmic = reset_pmic ? 1 : 0,
    };
    regmap_set_region_data(REGMAP_REGION_POWER_CTRL, &p, sizeof(p));
    utest_regmap_mark_region_changed(REGMAP_REGION_POWER_CTRL);
}

// ======================== wbec_do_periodic_work: WAIT_STARTUP ========================

// Сценарий: vmon не готов → остаёмся в WAIT_STARTUP.
// Ожидание: linux_cpu_pwr_seq_init не вызывается.
static void test_periodic_wait_startup_vmon_not_ready(void)
{
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);
    wbec_init();

    // vmon_ready = false по умолчанию после reset
    wbec_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(utest_linux_pwr_get_init_called(),
                              "linux_cpu_pwr_seq_init must not be called when vmon is not ready");
}

// Сценарий: vmon готов, POWER_ON, V33 есть → linux_cpu_pwr_seq_init(1), переход в POWER_ON_SEQUENCE_WAIT.
// Ожидание: init вызван с on=true, initial_powered_on = true (проверим через linux_booted в WORKING).
static void test_periodic_wait_startup_power_on_with_v33(void)
{
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);
    wbec_init();

    utest_vmon_set_ready(true);
    utest_vmon_set_ch_status(VMON_CHANNEL_V33, true);
    wbec_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_init_called(),
                             "linux_cpu_pwr_seq_init must be called when vmon is ready");
    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_init_on(),
                             "linux_cpu_pwr_seq_init must be called with on=true when V33 is present");

    // LED должен быть в режиме POWER_ON_SEQUENCE_WAIT: blink(50, 50)
    uint16_t on_ms, off_ms;
    utest_system_led_get_blink_params(&on_ms, &off_ms);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(50, on_ms,
                                     "LED on_ms must be 50 in POWER_ON_SEQUENCE_WAIT");
}

// Сценарий: vmon готов, POWER_ON, V33 нет → linux_cpu_pwr_seq_init(0), переход в VOLTAGE_CHECK.
// Ожидание: init вызван с on=false.
static void test_periodic_wait_startup_power_on_without_v33(void)
{
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);
    wbec_init();

    utest_vmon_set_ready(true);
    wbec_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_init_called(),
                             "linux_cpu_pwr_seq_init must be called when vmon is ready");
    TEST_ASSERT_FALSE_MESSAGE(utest_linux_pwr_get_init_on(),
                              "linux_cpu_pwr_seq_init must be called with on=false when V33 is absent");
}

// Сценарий: vmon готов, не POWER_ON (RTC_ALARM), V33 есть → init(0), VOLTAGE_CHECK.
// Ожидание: только POWER_ON причина приводит к init(1) при наличии V33.
static void test_periodic_wait_startup_non_power_on_with_v33(void)
{
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_RTC_ALARM);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);
    wbec_init();

    utest_vmon_set_ready(true);
    utest_vmon_set_ch_status(VMON_CHANNEL_V33, true);
    wbec_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_init_called(),
                             "linux_cpu_pwr_seq_init must be called");
    TEST_ASSERT_FALSE_MESSAGE(utest_linux_pwr_get_init_on(),
                              "linux_cpu_pwr_seq_init must be called with on=false for non-POWER_ON reason");
}

// ======================== wbec_do_periodic_work: VOLTAGE_CHECK ========================

// Сценарий: поле v_in в ADC_DATA зависит от статуса VMON_CHANNEL_V_IN.
// Ожидание: при валидном канале v_in берётся из ADC_CHANNEL_ADC_V_IN,
// иначе принудительно устанавливается в 0.
static void test_periodic_adc_vin_depends_on_vmon_status(void)
{
    const uint16_t expected_vin_mv = 15678;

    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_V_IN, expected_vin_mv);

    wbec_init();

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);
    struct REGMAP_ADC_DATA adc_data = get_adc_data_from_regmap();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(expected_vin_mv, adc_data.v_in,
                                     "v_in must be read from ADC_CHANNEL_ADC_V_IN when VMON_CHANNEL_V_IN is valid");

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, false);
    adc_data = get_adc_data_from_regmap();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, adc_data.v_in,
                                     "v_in must be 0 when VMON_CHANNEL_V_IN is invalid");
}

// Сценарий: заполнение ADC_DATA для vbus_network зависит от EC_USB_HUB_DEBUG_NETWORK.
// Ожидание: при включённом флаге vbus_network всегда 0; иначе читается из ADC_CHANNEL_ADC_VBUS_NETWORK.
static void test_periodic_adc_vbus_network_depends_on_debug_network_flag(void)
{
    const uint16_t expected_vbus_console_mv = 4321;
#if !defined(EC_USB_HUB_DEBUG_NETWORK)
    const uint16_t expected_vbus_network_mv = 1234;
#endif

    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBUS_DEBUG, expected_vbus_console_mv);

#if !defined(EC_USB_HUB_DEBUG_NETWORK)
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_VBUS_NETWORK, expected_vbus_network_mv);
#endif

    wbec_init();

    struct REGMAP_ADC_DATA adc_data = get_adc_data_from_regmap();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(expected_vbus_console_mv, adc_data.vbus_console,
                                     "vbus_console must be taken from ADC_CHANNEL_ADC_VBUS_DEBUG");

#if defined(EC_USB_HUB_DEBUG_NETWORK)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, adc_data.vbus_network,
                                     "vbus_network must be 0 when EC_USB_HUB_DEBUG_NETWORK is defined");
#else
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(expected_vbus_network_mv, adc_data.vbus_network,
                                     "vbus_network must be taken from ADC_CHANNEL_ADC_VBUS_NETWORK");
#endif
}

// Сценарий: USB-задержка при включении по POWER_ON с VBUS_DEBUG.
// Ожидание: остаёмся в VOLTAGE_CHECK пока не пройдёт 5000мс.
static void test_periodic_voltage_check_usb_delay(void)
{
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);
    wbec_init();

    // WAIT_STARTUP → VOLTAGE_CHECK
    utest_vmon_set_ready(true);
    wbec_do_periodic_work();

    // Включаем VBUS_DEBUG
    utest_vmon_set_ch_status(VMON_CHANNEL_VBUS_DEBUG, true);

    // Ещё не прошло 5000мс → должны остаться в VOLTAGE_CHECK
    wbec_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(utest_linux_pwr_get_pwr_on_called(),
                              "linux_cpu_pwr_seq_on must not be called during USB delay");

    // Через 5001мс → переход дальше
    utest_systick_advance_time_ms(5001);
    wbec_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_pwr_on_called(),
                             "linux_cpu_pwr_seq_on must be called after USB delay expires");
}

// Сценарий: температура готова → linux_cpu_pwr_seq_on и переход.
// Ожидание: linux_cpu_pwr_seq_on вызван, переход в POWER_ON_SEQUENCE_WAIT.
static void test_periodic_voltage_check_temp_ready(void)
{
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);
    wbec_init();

    utest_vmon_set_ready(true);
    wbec_do_periodic_work(); // → VOLTAGE_CHECK
    wbec_do_periodic_work(); // → POWER_ON_SEQUENCE_WAIT (temp ready по умолчанию)

    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_pwr_on_called(),
                             "linux_cpu_pwr_seq_on must be called when temperature is ready");
}

// Сценарий: температура не готова → переход в TEMP_CHECK_LOOP.
// Ожидание: linux_cpu_pwr_seq_on не вызван.
static void test_periodic_voltage_check_temp_not_ready(void)
{
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);
    wbec_init();

    utest_vmon_set_ready(true);
    wbec_do_periodic_work(); // → VOLTAGE_CHECK

    utest_temp_set_ready(false);
    wbec_do_periodic_work(); // → TEMP_CHECK_LOOP

    TEST_ASSERT_FALSE_MESSAGE(utest_linux_pwr_get_pwr_on_called(),
                              "linux_cpu_pwr_seq_on must not be called when temperature is not ready");
}

// ======================== wbec_do_periodic_work: TEMP_CHECK_LOOP ========================

// Сценарий: менее 5 секунд в TEMP_CHECK_LOOP → ничего не происходит.
// Ожидание: linux_cpu_pwr_seq_on не вызван.
static void test_periodic_temp_check_stays_under_5s(void)
{
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);
    wbec_init();

    utest_vmon_set_ready(true);
    wbec_do_periodic_work(); // → VOLTAGE_CHECK

    utest_temp_set_ready(false);
    wbec_do_periodic_work(); // → TEMP_CHECK_LOOP

    utest_systick_advance_time_ms(4999);
    wbec_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(utest_linux_pwr_get_pwr_on_called(),
                              "linux_cpu_pwr_seq_on must not be called before 5s in TEMP_CHECK_LOOP");
}

// Сценарий: >5с и температура стала OK.
// Ожидание: linux_cpu_pwr_seq_on вызван.
static void test_periodic_temp_check_recovers_after_5s(void)
{
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);
    wbec_init();

    utest_vmon_set_ready(true);
    wbec_do_periodic_work(); // → VOLTAGE_CHECK

    utest_temp_set_ready(false);
    wbec_do_periodic_work(); // → TEMP_CHECK_LOOP

    utest_systick_advance_time_ms(5001);
    utest_temp_set_ready(true);
    wbec_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_pwr_on_called(),
                             "linux_cpu_pwr_seq_on must be called when temp recovers after 5s");
}

// Сценарий: >5с, температура всё ещё не готова → остаёмся в TEMP_CHECK_LOOP.
// Ожидание: linux_cpu_pwr_seq_on не вызван, таймер сброшен.
static void test_periodic_temp_check_stays_if_still_cold(void)
{
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);
    wbec_init();

    utest_vmon_set_ready(true);
    wbec_do_periodic_work(); // → VOLTAGE_CHECK

    utest_temp_set_ready(false);
    wbec_do_periodic_work(); // → TEMP_CHECK_LOOP

    utest_systick_advance_time_ms(5001);
    wbec_do_periodic_work(); // Температура не готова → re-enter TEMP_CHECK_LOOP

    TEST_ASSERT_FALSE_MESSAGE(utest_linux_pwr_get_pwr_on_called(),
                              "linux_cpu_pwr_seq_on must not be called when temp is still not ready");

    // После ре-входа в TEMP_CHECK_LOOP: нужно ещё 5с
    utest_systick_advance_time_ms(4999);
    utest_temp_set_ready(true);
    wbec_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(utest_linux_pwr_get_pwr_on_called(),
                              "linux_cpu_pwr_seq_on must not be called before new 5s timeout");
}

// ======================== wbec_do_periodic_work: POWER_ON_SEQUENCE_WAIT ========================

// Сценарий: power sequence ещё busy → остаёмся в POWER_ON_SEQUENCE_WAIT.
// Ожидание: WDT не запущен, не переходим в WORKING.
static void test_periodic_power_on_seq_wait_stays_when_busy(void)
{
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);
    wbec_init();

    utest_vmon_set_ready(true);
    wbec_do_periodic_work(); // → VOLTAGE_CHECK
    wbec_do_periodic_work(); // → POWER_ON_SEQUENCE_WAIT

    utest_linux_pwr_set_busy(true);
    utest_vmon_set_ch_status(VMON_CHANNEL_V33, true);
    wbec_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(utest_wdt_get_started(),
                              "WDT must not be started while power sequence is busy");
}

// Сценарий: power sequence завершилась → переход в WORKING, WDT запущен.
// Ожидание: WDT запущен с WBEC_WATCHDOG_INITIAL_TIMEOUT_S.
static void test_periodic_power_on_seq_wait_transitions_to_working(void)
{
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);
    wbec_init();

    utest_vmon_set_ready(true);
    wbec_do_periodic_work(); // → VOLTAGE_CHECK
    wbec_do_periodic_work(); // → POWER_ON_SEQUENCE_WAIT

    utest_vmon_set_ch_status(VMON_CHANNEL_V33, true);
    wbec_do_periodic_work(); // → WORKING

    TEST_ASSERT_TRUE_MESSAGE(utest_wdt_get_started(),
                             "WDT must be started in WORKING state");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(WBEC_WATCHDOG_INITIAL_TIMEOUT_S, utest_wdt_get_timeout(),
                                     "WDT timeout must match WBEC_WATCHDOG_INITIAL_TIMEOUT_S");

    // LED должен быть в режиме WORKING: blink(500, 1000)
    uint16_t on_ms, off_ms;
    utest_system_led_get_blink_params(&on_ms, &off_ms);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(500, on_ms,
                                     "LED on_ms must be 500 in WORKING state");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1000, off_ms,
                                     "LED off_ms must be 1000 in WORKING state");
}

// Сценарий: initial_powered_on = true (V33 был при старте).
// Ожидание: при переходе в WORKING linux_booted сразу true (нет ожидания 20с).
// Проверяем: нажатие кнопки приводит к IRQ, а не к hard_off.
static void test_periodic_power_on_seq_initial_powered_on_sets_booted(void)
{
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);
    wbec_init();

    // WAIT_STARTUP → POWER_ON_SEQUENCE_WAIT (путь с V33, initial_powered_on=true)
    utest_vmon_set_ready(true);
    utest_vmon_set_ch_status(VMON_CHANNEL_V33, true);
    wbec_do_periodic_work();

    // POWER_ON_SEQUENCE_WAIT → WORKING
    wbec_do_periodic_work();

    // Проверяем, что linux_booted=true: нажатие кнопки → IRQ, а не hard_off
    utest_pwrkey_set_pressed(true);
    wbec_do_periodic_work();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE((1u << IRQ_PWR_OFF_REQ), utest_irq_get_all_flags(),
                                     "IRQ_PWR_OFF_REQ must be set when linux_booted=true and button pressed");
    TEST_ASSERT_FALSE_MESSAGE(utest_linux_pwr_get_hard_off_called(),
                              "hard_off must not be called when linux is already booted");
}

// ======================== wbec_do_periodic_work: WORKING ========================

// Сценарий: Linux загружен (>20с), кнопка нажата → IRQ_PWR_OFF_REQ.
static void test_periodic_working_booted_pwrkey_sends_irq(void)
{
    drive_to_working_state();

    // Ждём загрузки Linux (>20с)
    utest_systick_advance_time_ms(WBEC_LINUX_BOOT_TIME_MS + 1);
    wbec_do_periodic_work();

    utest_pwrkey_set_pressed(true);
    wbec_do_periodic_work();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE((1u << IRQ_PWR_OFF_REQ), utest_irq_get_all_flags(),
                                     "IRQ_PWR_OFF_REQ must be set when booted and button pressed");
}

// Сценарий: Linux не загружен (<20с), короткое нажатие → hard_off.
static void test_periodic_working_not_booted_short_press_hard_off(void)
{
    drive_to_working_state();

    utest_pwrkey_set_short_press(true);
    wbec_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_hard_off_called(),
                             "hard_off must be called on short press before linux boot");
}

// Сценарий: Linux загружается по времени (>20с).
// Ожидание: после 20с linux_booted=true, нажатие кнопки → IRQ.
static void test_periodic_working_linux_boots_after_timeout(void)
{
    drive_to_working_state();

    // До 20с: не загружен, short_press → hard_off
    // Проверяем, что до 20с linux не считается загруженным
    utest_pwrkey_set_pressed(true);
    wbec_do_periodic_work();

    // До 20с IRQ не выставляется
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, utest_irq_get_all_flags(),
                                     "IRQ must not be set before linux boot timeout");

    utest_pwrkey_set_pressed(false);

    // Через 20001мс: загружен
    utest_systick_advance_time_ms(WBEC_LINUX_BOOT_TIME_MS + 1);
    wbec_do_periodic_work();

    utest_pwrkey_set_pressed(true);
    wbec_do_periodic_work();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE((1u << IRQ_PWR_OFF_REQ), utest_irq_get_all_flags(),
                                     "IRQ_PWR_OFF_REQ must be set after linux boot timeout");
}

// Сценарий: таймаут флага нажатия кнопки (90с) → флаг сбрасывается.
// Ожидание: после 90с pwrkey_pressed очищается, poweroff с кнопкой не будет.
static void test_periodic_working_pwrkey_flag_timeout(void)
{
    drive_to_working_state();

    // Дождёмся загрузки
    utest_systick_advance_time_ms(WBEC_LINUX_BOOT_TIME_MS + 1);
    wbec_do_periodic_work();

    // Нажимаем кнопку
    utest_pwrkey_set_pressed(true);
    wbec_do_periodic_work();
    utest_pwrkey_set_pressed(false);

    // Через 90с флаг должен сброситься
    utest_systick_advance_time_ms(WBEC_LINUX_POWER_OFF_DELAY_MS + 1);
    wbec_do_periodic_work();

    // Запрос poweroff без alarm/wbmz → должен reboot (т.к. btn=false уже)
    set_power_ctrl_request(true, false, false);
    wbec_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_hard_reset_called(),
                             "hard_reset must be called when poweroff without alarm/wbmz/btn");
    TEST_ASSERT_FALSE_MESSAGE(utest_linux_pwr_get_hard_off_called(),
                              "hard_off must not be called when btn flag timed out");
}

// Сценарий: запрос poweroff из Linux при наличии alarm → hard_off.
static void test_periodic_working_poweroff_with_alarm(void)
{
    drive_to_working_state();

    utest_rtc_alarm_set_enabled(true);
    set_power_ctrl_request(true, false, false);
    wbec_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_hard_off_called(),
                             "hard_off must be called on poweroff request with alarm set");
}

// Сценарий: запрос poweroff из Linux при питании от WBMZ → hard_off.
static void test_periodic_working_poweroff_with_wbmz(void)
{
    drive_to_working_state();

    utest_wbmz_set_powered_from_wbmz(true);
    set_power_ctrl_request(true, false, false);
    wbec_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_hard_off_called(),
                             "hard_off must be called on poweroff request when powered from WBMZ");
}

// Сценарий: запрос poweroff из Linux при нажатой кнопке → hard_off.
static void test_periodic_working_poweroff_with_button(void)
{
    drive_to_working_state();

    // Загружаемся и нажимаем кнопку
    utest_systick_advance_time_ms(WBEC_LINUX_BOOT_TIME_MS + 1);
    wbec_do_periodic_work();

    utest_pwrkey_set_pressed(true);
    wbec_do_periodic_work();
    utest_pwrkey_set_pressed(false);

    set_power_ctrl_request(true, false, false);
    wbec_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_hard_off_called(),
                             "hard_off must be called on poweroff with button press");
}

// Сценарий: запрос poweroff без alarm/wbmz/button → reboot вместо poweroff.
// Ожидание: hard_reset, reason=REBOOT_NO_ALARM.
static void test_periodic_working_poweroff_without_conditions_reboots(void)
{
    drive_to_working_state();

    set_power_ctrl_request(true, false, false);
    wbec_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_hard_reset_called(),
                             "hard_reset must be called on poweroff without alarm/wbmz/btn");
    TEST_ASSERT_FALSE_MESSAGE(utest_linux_pwr_get_hard_off_called(),
                              "hard_off must not be called on poweroff without conditions");

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(4, get_poweron_reason_from_regmap(),
                                     "poweron_reason must be REASON_REBOOT_NO_ALARM (4)");
}

// Сценарий: запрос reboot из Linux → hard_reset.
static void test_periodic_working_reboot_request(void)
{
    drive_to_working_state();

    set_power_ctrl_request(false, true, false);
    wbec_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_hard_reset_called(),
                             "hard_reset must be called on reboot request");

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3, get_poweron_reason_from_regmap(),
                                     "poweron_reason must be REASON_REBOOT (3)");
}

// Сценарий: запрос pmic_reset из Linux → reset_pmic.
static void test_periodic_working_pmic_reset_request(void)
{
    drive_to_working_state();

    set_power_ctrl_request(false, false, true);
    wbec_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_reset_pmic_called(),
                             "reset_pmic must be called on pmic_reset request");

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3, get_poweron_reason_from_regmap(),
                                     "poweron_reason must be REASON_REBOOT (3)");
}

// Сценарий: WDT тайм-аут → hard_reset, reason=WATCHDOG.
static void test_periodic_working_wdt_timeout(void)
{
    drive_to_working_state();

    utest_wdt_set_timed_out(true);
    wbec_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_hard_reset_called(),
                             "hard_reset must be called on WDT timeout");

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(5, get_poweron_reason_from_regmap(),
                                     "poweron_reason must be REASON_WATCHDOG (5)");
}

// Сценарий: пропало 3.3В и 5В одновременно → hard_off (питание выдернуто).
static void test_periodic_working_v33_and_v50_lost(void)
{
    drive_to_working_state();

    utest_vmon_set_ch_status(VMON_CHANNEL_V33, false);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, false);
    wbec_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_hard_off_called(),
                             "hard_off must be called when both V33 and V50 are lost");
}

// Сценарий: пропало 3.3В, питание от WBMZ, батарея разряжена → disable stepup + hard_reset.
static void test_periodic_working_v33_lost_wbmz_vbat_low(void)
{
    drive_to_working_state();

    utest_vmon_set_ch_status(VMON_CHANNEL_V33, false);
    utest_wbmz_set_powered_from_wbmz(true);
    utest_wbmz_set_vbat_ok(false);
    wbec_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(utest_wbmz_get_stepup_enabled(),
                              "WBMZ stepup must be disabled when vbat is low");
    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_hard_reset_called(),
                             "hard_reset must be called when V33 lost and WBMZ vbat is low");
}

// Сценарий: пропало 3.3В, первая попытка → enable stepup + hard_reset, reason=PMIC_OFF.
static void test_periodic_working_v33_lost_first_attempt(void)
{
    drive_to_working_state();

    utest_vmon_set_ch_status(VMON_CHANNEL_V33, false);
    wbec_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_wbmz_get_stepup_enabled(),
                             "WBMZ stepup must be enabled on first V33 loss");
    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_hard_reset_called(),
                             "hard_reset must be called on first V33 loss");

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(6, get_poweron_reason_from_regmap(),
                                     "poweron_reason must be REASON_PMIC_OFF (6)");
}

// Сценарий: пропало 3.3В больше WBEC_POWER_LOSS_ATTEMPTS раз за WBEC_POWER_LOSS_TIMEOUT_MIN минут → hard_off.
static void test_periodic_working_v33_loss_exceeds_limit(void)
{
    drive_to_working_state();

    // Симулируем WBEC_POWER_LOSS_ATTEMPTS + 1 потерь 3.3В подряд в пределах таймаута
    for (unsigned i = 0; i <= WBEC_POWER_LOSS_ATTEMPTS; i++) {
        utest_linux_pwr_reset();
        utest_vmon_set_ch_status(VMON_CHANNEL_V33, false);
        wbec_do_periodic_work();

        if (i < WBEC_POWER_LOSS_ATTEMPTS) {
            // Ещё в пределах лимита → hard_reset
            TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_hard_reset_called(),
                                     "hard_reset must be called within power loss limit");
            // Переход: POWER_ON_SEQUENCE_WAIT → WORKING
            utest_vmon_set_ch_status(VMON_CHANNEL_V33, true);
            wbec_do_periodic_work(); // → WORKING
        }
    }

    // На последнем разе должен быть hard_off
    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_hard_off_called(),
                             "hard_off must be called when V33 loss limit is exceeded");
}

// Сценарий: пропало 3.3В, но потери происходят с большим интервалом → счётчик сбрасывается.
// Ожидание: после WBEC_POWER_LOSS_TIMEOUT_MIN минут между потерями счётчик обнуляется,
// и лимит не превышается.
static void test_periodic_working_v33_loss_counter_resets_after_timeout(void)
{
    drive_to_working_state();

    // Первая потеря
    utest_vmon_set_ch_status(VMON_CHANNEL_V33, false);
    wbec_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_hard_reset_called(),
                             "hard_reset must be called on first V33 loss");

    // Восстановление: POWER_ON_SEQUENCE_WAIT → WORKING
    utest_linux_pwr_reset();
    utest_vmon_set_ch_status(VMON_CHANNEL_V33, true);
    wbec_do_periodic_work();

    // Ждём дольше WBEC_POWER_LOSS_TIMEOUT_MIN минут, чтобы счётчик сбросился
    utest_systick_advance_time_ms((WBEC_POWER_LOSS_TIMEOUT_MIN * 60 * 1000) + 1);
    wbec_do_periodic_work(); // Нужен для обновления внутренних таймеров

    // Вторая потеря (после сброса счётчика) → hard_reset, а не hard_off
    utest_linux_pwr_reset();
    utest_vmon_set_ch_status(VMON_CHANNEL_V33, false);
    wbec_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_hard_reset_called(),
                             "hard_reset must be called after counter reset by timeout");
    TEST_ASSERT_FALSE_MESSAGE(utest_linux_pwr_get_hard_off_called(),
                              "hard_off must not be called when counter was reset by timeout");
}

// ======================== wbec_do_periodic_work: POWER_OFF_SEQUENCE_WAIT ========================

// Сценарий: в POWER_OFF_SEQUENCE_WAIT ничего не происходит.
static void test_periodic_power_off_seq_wait_does_nothing(void)
{
    drive_to_working_state();

    // Переводим в POWER_OFF_SEQUENCE_WAIT через hard_off
    utest_pwrkey_set_short_press(true);
    wbec_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(utest_linux_pwr_get_hard_off_called(),
                             "hard_off must be called to enter POWER_OFF_SEQUENCE_WAIT");

    // Сбрасываем флаги стабов для контроля
    utest_linux_pwr_reset();
    utest_irq_reset();

    wbec_do_periodic_work();
    wbec_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(utest_linux_pwr_get_hard_off_called(),
                              "No actions in POWER_OFF_SEQUENCE_WAIT");
    TEST_ASSERT_FALSE_MESSAGE(utest_linux_pwr_get_hard_reset_called(),
                              "No actions in POWER_OFF_SEQUENCE_WAIT");
    TEST_ASSERT_FALSE_MESSAGE(utest_linux_pwr_get_standby_called(),
                              "No standby transition in POWER_OFF_SEQUENCE_WAIT");
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
    RUN_TEST(test_init_power_key_waits_for_pwrkey_ready);

    // Включение по будильнику RTC
    RUN_TEST(test_init_rtc_alarm);

    // Периодическое пробуждение RTC
    RUN_TEST(test_init_periodic_wakeup_5v_appeared);
    RUN_TEST(test_init_periodic_wakeup_3v3_present_powers_on);
    RUN_TEST(test_init_periodic_wakeup_5v_was_on_no_3v3_goes_to_standby);
    RUN_TEST(test_init_periodic_wakeup_no_5v_was_on_saves_state);
    RUN_TEST(test_init_periodic_wakeup_no_5v_was_off_goes_to_standby);
    RUN_TEST(test_init_periodic_wakeup_no_5v_was_off_no_extra_save);

    // Общие проверки init
    RUN_TEST(test_init_enables_stepup_when_no_5v);
    RUN_TEST(test_init_does_not_enable_stepup_when_5v_present);

    // WAIT_STARTUP
    RUN_TEST(test_periodic_wait_startup_vmon_not_ready);
    RUN_TEST(test_periodic_wait_startup_power_on_with_v33);
    RUN_TEST(test_periodic_wait_startup_power_on_without_v33);
    RUN_TEST(test_periodic_wait_startup_non_power_on_with_v33);

    // VOLTAGE_CHECK
    RUN_TEST(test_periodic_adc_vin_depends_on_vmon_status);
    RUN_TEST(test_periodic_adc_vbus_network_depends_on_debug_network_flag);
    RUN_TEST(test_periodic_voltage_check_usb_delay);
    RUN_TEST(test_periodic_voltage_check_temp_ready);
    RUN_TEST(test_periodic_voltage_check_temp_not_ready);

    // TEMP_CHECK_LOOP
    RUN_TEST(test_periodic_temp_check_stays_under_5s);
    RUN_TEST(test_periodic_temp_check_recovers_after_5s);
    RUN_TEST(test_periodic_temp_check_stays_if_still_cold);

    // POWER_ON_SEQUENCE_WAIT
    RUN_TEST(test_periodic_power_on_seq_wait_stays_when_busy);
    RUN_TEST(test_periodic_power_on_seq_wait_transitions_to_working);
    RUN_TEST(test_periodic_power_on_seq_initial_powered_on_sets_booted);

    // WORKING
    RUN_TEST(test_periodic_working_booted_pwrkey_sends_irq);
    RUN_TEST(test_periodic_working_not_booted_short_press_hard_off);
    RUN_TEST(test_periodic_working_linux_boots_after_timeout);
    RUN_TEST(test_periodic_working_pwrkey_flag_timeout);
    RUN_TEST(test_periodic_working_poweroff_with_alarm);
    RUN_TEST(test_periodic_working_poweroff_with_wbmz);
    RUN_TEST(test_periodic_working_poweroff_with_button);
    RUN_TEST(test_periodic_working_poweroff_without_conditions_reboots);
    RUN_TEST(test_periodic_working_reboot_request);
    RUN_TEST(test_periodic_working_pmic_reset_request);
    RUN_TEST(test_periodic_working_wdt_timeout);
    RUN_TEST(test_periodic_working_v33_and_v50_lost);
    RUN_TEST(test_periodic_working_v33_lost_wbmz_vbat_low);
    RUN_TEST(test_periodic_working_v33_lost_first_attempt);
    RUN_TEST(test_periodic_working_v33_loss_exceeds_limit);
    RUN_TEST(test_periodic_working_v33_loss_counter_resets_after_timeout);

    // POWER_OFF_SEQUENCE_WAIT
    RUN_TEST(test_periodic_power_off_seq_wait_does_nothing);

    return UNITY_END();
}
