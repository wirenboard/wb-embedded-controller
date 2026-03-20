#include "unity.h"

#include "linux-power-control.h"
#include "config.h"
#include "mcu-pwr.h"
#include "voltage-monitor.h"
#include "utest_gpio.h"
#include "utest_mcu_pwr.h"
#include "utest_pwrkey.h"
#include "utest_systick.h"
#include "utest_voltage_monitor.h"
#include "utest_wdt_stm32.h"
#include "utest_wbmcu_system.h"
#include "utest_wbmz_common.h"

void utest_linux_power_control_reset_state(void);

static const gpio_pin_t linux_power_gpio = {EC_GPIO_LINUX_POWER};
static const gpio_pin_t pmic_pwron_gpio = {EC_GPIO_LINUX_PMIC_PWRON};
static const gpio_pin_t pmic_reset_gpio = {EC_GPIO_LINUX_PMIC_RESET_PWROK};

static bool release_pwrkey_from_watchdog = false;

static void watchdog_reload_callback(void)
{
    if (release_pwrkey_from_watchdog) {
        utest_set_pwrkey_pressed(false);
    }
}

static void prepare_periodic_runtime(bool v50_status, bool v33_status)
{
    utest_vmon_set_ready(true);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, v50_status);
    utest_vmon_set_ch_status(VMON_CHANNEL_V33, v33_status);
}

void setUp(void)
{
    utest_gpio_reset_instances();
    utest_mcu_reset();
    utest_systick_set_time_ms(0);
    utest_vmon_reset();
    utest_pwr_reset();
    utest_watchdog_reset();
    utest_linux_power_control_reset_state();
    utest_wbmz_common_reset();
    utest_pwrkey_reset();
    utest_watchdog_set_reload_callback(watchdog_reload_callback);
}

void tearDown(void)
{
}

// ==================== Базовые ранние выходы и инициализация ====================

// Сценарий: после init(false) и готового vmon модуль ничего не меняет.
// До действия: питание/PMIC линии уже в низком уровне, standby ещё не запрошен.
// После действия: значения остаются теми же, лишних side effect нет.
static void test_periodic_init_off_state_does_not_change_outputs(void)
{
    linux_cpu_pwr_seq_init(false);
    prepare_periodic_runtime(true, false);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(linux_power_gpio), "Linux power GPIO must be low before periodic work"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be low before periodic work"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_reset_gpio), "PMIC RESET GPIO must be low before periodic work"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, utest_mcu_get_standby_wakeup_time(), "Standby must not be requested before periodic work"
    );

    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(linux_power_gpio), "Linux power GPIO must stay low in init-off state"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must stay low in init-off state"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_reset_gpio), "PMIC RESET GPIO must stay low in init-off state"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, utest_mcu_get_standby_wakeup_time(), "Standby must not be requested in init-off periodic state"
    );
}

// Сценарий: вызов periodic_work до init при готовом vmon.
// До действия: модуль не инициализирован, линии в дефолтном состоянии.
// После действия: ранний выход по !initialized, побочных эффектов нет.
static void test_periodic_returns_early_when_module_not_initialized(void)
{
    prepare_periodic_runtime(true, true);

    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, utest_mcu_get_standby_wakeup_time(), "Standby must not be requested before init"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(linux_power_gpio), "Linux power GPIO must not change before init"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must not change before init"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_reset_gpio), "PMIC RESET GPIO must not change before init"
    );
}

// Сценарий: vmon не готов после init.
// До действия: питание уже включено init(true), standby не запрошен.
// После действия: ранний выход по !vmon_ready без побочных эффектов.
static void test_periodic_returns_early_when_vmon_is_not_ready(void)
{
    linux_cpu_pwr_seq_init(true);
    utest_vmon_set_ready(false);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, false);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(linux_power_gpio), "Linux power GPIO must be high after init(true)"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0,
        utest_gpio_get_output_state(pmic_pwron_gpio),
        "PMIC PWRON GPIO must be low before periodic work when vmon is not ready"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0,
        utest_gpio_get_output_state(pmic_reset_gpio),
        "PMIC RESET GPIO must be low before periodic work when vmon is not ready"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, utest_mcu_get_standby_wakeup_time(), "Standby must not be requested before periodic work"
    );

    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, utest_mcu_get_standby_wakeup_time(), "Standby must not be requested when vmon is not ready"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(linux_power_gpio), "Linux power GPIO must keep init state when vmon is not ready"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must not change when vmon is not ready"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_reset_gpio), "PMIC RESET GPIO must not change when vmon is not ready"
    );
}

// Сценарий: граничное условие таймаута шага PS_ON_STEP1_WAIT_3V3.
// До действия: 3.3V отсутствует, алгоритм ждёт в step1.
// После действия: в 1000мс перехода нет, в 1001мс активируется fallback с PMIC PWRON.
static void test_periodic_step1_timeout_changes_state_only_after_1000ms(void)
{
    linux_cpu_pwr_seq_init(true);
    prepare_periodic_runtime(true, false);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be low at step1 start"
    );

    utest_systick_advance_time_ms(1000);
    linux_cpu_pwr_seq_do_periodic_work();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must stay low at exact 1000ms in step1"
    );

    utest_systick_advance_time_ms(1);
    linux_cpu_pwr_seq_do_periodic_work();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1,
        utest_gpio_get_output_state(pmic_pwron_gpio),
        "PMIC PWRON GPIO must go high only after 1000ms timeout is exceeded"
    );
}

// Сценарий: граничное условие таймаута шага PS_ON_STEP2_PMIC_PWRON.
// До действия: fallback уже включил PMIC PWRON и алгоритм находится в step2.
// После действия: в 1500мс PMIC PWRON остаётся активным, в 1501мс происходит выход из step2 и PWRON сбрасывается.
static void test_periodic_step2_timeout_changes_state_only_after_1500ms(void)
{
    linux_cpu_pwr_seq_init(true);
    prepare_periodic_runtime(true, false);

    utest_systick_advance_time_ms(1001);
    linux_cpu_pwr_seq_do_periodic_work();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be high at step2 start"
    );

    utest_systick_advance_time_ms(1500);
    linux_cpu_pwr_seq_do_periodic_work();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must stay high at exact 1500ms in step2"
    );

    utest_systick_advance_time_ms(1);
    linux_cpu_pwr_seq_do_periodic_work();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0,
        utest_gpio_get_output_state(pmic_pwron_gpio),
        "PMIC PWRON GPIO must go low only after 1500ms timeout is exceeded"
    );
}

// Сценарий: успешное включение при появлении 3.3V до таймаута.
// До действия: запущен шаг ожидания 3.3В, алгоритм занят.
// После действия: алгоритм завершает включение, busy=false.
static void test_periodic_power_on_success_when_v33_is_present(void)
{
    linux_cpu_pwr_seq_init(true);
    prepare_periodic_runtime(true, true);

    TEST_ASSERT_TRUE_MESSAGE(
        linux_cpu_pwr_seq_is_busy(), "Power sequence must be busy before first periodic work call"
    );

    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(linux_cpu_pwr_seq_is_busy(), "Power sequence must finish when 3.3V is present");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(linux_power_gpio), "Linux power GPIO must stay high after successful power-on"
    );
}

// Сценарий: 3.3V не появляется >1000мс на первом шаге включения.
// До действия: PMIC PWRON отпущен.
// После действия: PMIC PWRON поднимается для fallback-сценария.
static void test_periodic_power_on_fallback_enables_pmic_pwron(void)
{
    linux_cpu_pwr_seq_init(true);
    prepare_periodic_runtime(true, false);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be low before fallback timeout"
    );

    utest_systick_advance_time_ms(1001);
    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(linux_cpu_pwr_seq_is_busy(), "Power sequence must stay busy in fallback path");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be high in fallback path"
    );
}

// Сценарий: после fallback на PWRON появляется 3.3V.
// До действия: fallback уже включил PMIC PWRON.
// После действия: PMIC PWRON выключается, алгоритм завершает включение.
static void test_periodic_power_on_fallback_completes_when_v33_appears(void)
{
    linux_cpu_pwr_seq_init(true);
    prepare_periodic_runtime(true, false);

    utest_systick_advance_time_ms(1001);
    linux_cpu_pwr_seq_do_periodic_work();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be high before fallback completion"
    );
    utest_vmon_set_ch_status(VMON_CHANNEL_V33, true);
    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(linux_cpu_pwr_seq_is_busy(), "Power sequence must complete after fallback success");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be low after fallback success"
    );
}

// ==================== Защита от повторного старта последовательности ====================

// Сценарий: повторный вызов linux_cpu_pwr_seq_on во время шага PS_ON_STEP2_PMIC_PWRON.
// Ожидаем: функция делает ранний return и не сбрасывает шаги включения.
static void test_linux_cpu_pwr_seq_on_returns_early_when_sequence_is_already_running(void)
{
    linux_cpu_pwr_seq_init(true);
    prepare_periodic_runtime(true, false);

    // Переходим на шаг PS_ON_STEP2_PMIC_PWRON, где PMIC PWRON уже поднят.
    utest_systick_advance_time_ms(1001);
    linux_cpu_pwr_seq_do_periodic_work();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be high in step2 before re-on call"
    );

    // Повторный вызов on() не должен перезапускать последовательность.
    linux_cpu_pwr_seq_on();

    // На шаге step2 при появлении 3.3V PMIC PWRON должен быть сброшен.
    // Если бы on() не сделал return и сбросил состояние на step1, PWRON остался бы высоким.
    utest_vmon_set_ch_status(VMON_CHANNEL_V33, true);
    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be low if on() returned in step2"
    );
    TEST_ASSERT_FALSE_MESSAGE(linux_cpu_pwr_seq_is_busy(), "Power sequence must complete after 3.3V appears in step2");
}

// Сценарий: повторный вызов linux_cpu_pwr_seq_on на шаге PS_ON_STEP1_WAIT_3V3.
// До действия: шаг ожидания 3.3В уже идёт.
// После действия: ранний return не сбрасывает timestamp шага включения.
static void test_linux_cpu_pwr_seq_on_returns_early_in_step1_without_timer_reset(void)
{
    linux_cpu_pwr_seq_init(true);
    prepare_periodic_runtime(true, false);

    utest_systick_advance_time_ms(900);
    linux_cpu_pwr_seq_on();
    utest_systick_advance_time_ms(101);
    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be high after total 1001ms in step1"
    );
}

// Сценарий: повторный вызов linux_cpu_pwr_seq_on на шаге PS_ON_STEP3_PMIC_PWRON_OFF_WAIT.
// До действия: шаг step3 уже активен, PWRON отпущен.
// После действия: ранний return не сбрасывает таймер step3, переход в step2 происходит по старому таймеру.
static void test_linux_cpu_pwr_seq_on_returns_early_in_step3_without_timer_reset(void)
{
    linux_cpu_pwr_seq_init(true);
    prepare_periodic_runtime(true, false);

    // Переходим в step2, затем по таймауту в step3.
    utest_systick_advance_time_ms(1001);
    linux_cpu_pwr_seq_do_periodic_work();
    utest_systick_advance_time_ms(1501);
    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be low in step3 before re-on call"
    );

    // Через 501 мс после входа в step3 должен быть возврат в step2.
    // Если on() перезапустит последовательность, этого не произойдет.
    utest_systick_advance_time_ms(400);
    linux_cpu_pwr_seq_on();
    utest_systick_advance_time_ms(101);
    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1,
        utest_gpio_get_output_state(pmic_pwron_gpio),
        "PMIC PWRON GPIO must be high if step3 timer was not reset by on()"
    );
}

// Сценарий: повторный вызов linux_cpu_pwr_seq_on в состоянии PS_ON_COMPLETE.
// До действия: последовательность уже завершена, busy=false.
// После действия: ранний return, повторный старт последовательности не происходит.
static void test_linux_cpu_pwr_seq_on_returns_early_in_on_complete_state(void)
{
    linux_cpu_pwr_seq_init(true);
    prepare_periodic_runtime(true, true);
    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(
        linux_cpu_pwr_seq_is_busy(), "Power sequence must be complete before re-on call in ON_COMPLETE"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be low before re-on call in ON_COMPLETE"
    );

    utest_vmon_set_ch_status(VMON_CHANNEL_V33, false);
    linux_cpu_pwr_seq_on();
    utest_systick_advance_time_ms(1001);
    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0,
        utest_gpio_get_output_state(pmic_pwron_gpio),
        "PMIC PWRON GPIO must stay low when on() is called in ON_COMPLETE"
    );
}

// ==================== Проверка статуса busy ====================

// Сценарий: проверка busy в состоянии PS_OFF_COMPLETE.
// До действия: после init(true) последовательность занята, 5V включен.
// После действия: после hard_off busy=false; 5V изменился на OFF, standby остался неизменным.
static void test_is_busy_returns_false_in_off_complete_state(void)
{
    linux_cpu_pwr_seq_init(true);

    TEST_ASSERT_TRUE_MESSAGE(linux_cpu_pwr_seq_is_busy(), "Power sequence must be busy right after init(true)");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(linux_power_gpio), "Linux power GPIO must be high before hard_off"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be low before hard_off"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, utest_mcu_get_standby_wakeup_time(), "Standby must not be requested before hard_off"
    );

    linux_cpu_pwr_seq_hard_off();

    TEST_ASSERT_FALSE_MESSAGE(linux_cpu_pwr_seq_is_busy(), "Power sequence must not be busy in OFF_COMPLETE state");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(linux_power_gpio), "Linux power GPIO must be low after hard_off"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must stay low after hard_off"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, utest_mcu_get_standby_wakeup_time(), "hard_off must not request standby by itself"
    );
}

// Сценарий: проверка busy в состоянии PS_ON_COMPLETE.
// До действия: после init(true) последовательность занята.
// После действия: после periodic_work с V33=ON busy=false; питание остаётся включенным, standby не запрашивается.
static void test_is_busy_returns_false_in_on_complete_state(void)
{
    linux_cpu_pwr_seq_init(true);
    prepare_periodic_runtime(true, true);

    TEST_ASSERT_TRUE_MESSAGE(
        linux_cpu_pwr_seq_is_busy(), "Power sequence must be busy before transition to ON_COMPLETE"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(linux_power_gpio), "Linux power GPIO must be high before ON_COMPLETE transition"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, utest_mcu_get_standby_wakeup_time(), "Standby must not be requested before ON_COMPLETE transition"
    );

    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(linux_cpu_pwr_seq_is_busy(), "Power sequence must not be busy in ON_COMPLETE state");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(linux_power_gpio), "Linux power GPIO must stay high in ON_COMPLETE state"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must remain low in ON_COMPLETE state"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, utest_mcu_get_standby_wakeup_time(), "ON_COMPLETE transition must not request standby"
    );
}

// ==================== Таймауты, сбросы и standby ====================

// Сценарий: 3.3V не появляется на всех попытках fallback.
// До действия: после init включение стартует в step1.
// После действия: после исчерпания попыток 5V выключается (сценарий reset 5V).
static void test_periodic_power_on_fallback_exhaustion_drops_5v(void)
{
    linux_cpu_pwr_seq_init(true);
    prepare_periodic_runtime(true, false);

    TEST_ASSERT_TRUE_MESSAGE(
        linux_cpu_pwr_seq_is_busy(), "Power sequence must be busy before fallback exhaustion flow"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1,
        utest_gpio_get_output_state(linux_power_gpio),
        "Linux power GPIO must be high before fallback exhaustion flow"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be low before fallback exhaustion flow"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, utest_mcu_get_standby_wakeup_time(), "Standby must not be requested before fallback exhaustion flow"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        MCU_VCC_5V_STATE_OFF, mcu_get_vcc_5v_last_state(), "Saved 5V state must keep default OFF before standby request"
    );

    utest_systick_advance_time_ms(1001);
    linux_cpu_pwr_seq_do_periodic_work();

    for (unsigned i = 0; i < 3; i++) {
        utest_systick_advance_time_ms(1501);
        linux_cpu_pwr_seq_do_periodic_work();
        utest_systick_advance_time_ms(501);
        linux_cpu_pwr_seq_do_periodic_work();
    }

    utest_systick_advance_time_ms(1501);
    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(linux_cpu_pwr_seq_is_busy(), "Power sequence must stay busy in reset-5V path");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(linux_power_gpio), "Linux power GPIO must be low after fallback exhaustion"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be low after fallback exhaustion"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, utest_mcu_get_standby_wakeup_time(), "Fallback exhaustion path must not request standby immediately"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        MCU_VCC_5V_STATE_OFF,
        mcu_get_vcc_5v_last_state(),
        "Saved 5V state must stay unchanged until standby path is executed"
    );
}

// Сценарий: граничное условие таймаута step3.
// До действия: step3 уже активен.
// После действия: при ровно 500мс повтора нет, при 501мс выполняется переход в step2.
static void test_periodic_step3_retry_happens_only_after_timeout_is_strictly_exceeded(void)
{
    linux_cpu_pwr_seq_init(true);
    prepare_periodic_runtime(true, false);

    // Переходим в step3.
    utest_systick_advance_time_ms(1001);
    linux_cpu_pwr_seq_do_periodic_work();
    utest_systick_advance_time_ms(1501);
    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be low at step3 start"
    );

    utest_systick_advance_time_ms(500);
    linux_cpu_pwr_seq_do_periodic_work();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must stay low at exact 500ms timeout in step3"
    );

    utest_systick_advance_time_ms(1);
    linux_cpu_pwr_seq_do_periodic_work();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must go high only after timeout is exceeded"
    );
}

// Сценарий: в reset-5V ожидании истекает таймаут reset.
// До действия: hard_reset выключил 5В и перевёл последовательность в ожидание.
// После действия: 5V снова включается, переход к следующему шагу включения.
static void test_periodic_hard_reset_recovery_reenables_5v_after_timeout(void)
{
    linux_cpu_pwr_seq_init(true);
    prepare_periodic_runtime(true, false);

    utest_systick_advance_time_ms(1001);
    linux_cpu_pwr_seq_do_periodic_work();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be high before hard reset"
    );

    linux_cpu_pwr_seq_hard_reset();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(linux_power_gpio), "Linux power GPIO must be low right after hard reset"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be low right after hard reset"
    );
    TEST_ASSERT_TRUE_MESSAGE(linux_cpu_pwr_seq_is_busy(), "Power sequence must be busy during hard reset wait");

    utest_systick_advance_time_ms(WBEC_POWER_RESET_TIME_MS);
    linux_cpu_pwr_seq_do_periodic_work();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(linux_power_gpio), "Linux power GPIO must stay low at exact reset timeout"
    );

    utest_systick_advance_time_ms(1);
    linux_cpu_pwr_seq_do_periodic_work();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(linux_power_gpio), "Linux power GPIO must be high after reset timeout elapses"
    );
}

// Сценарий: в PS_OFF_COMPLETE истекает задержка >200мс.
// До действия: система уже переведена в OFF_COMPLETE, standby не запрошен.
// После действия: сохраняется статус 5V=ON и вызывается переход в standby.
static void test_periodic_hard_off_timeout_goes_to_standby_with_v50_on(void)
{
    linux_cpu_pwr_seq_init(true);
    linux_cpu_pwr_seq_hard_off();
    prepare_periodic_runtime(true, false);

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, utest_mcu_get_standby_wakeup_time(), "Standby must not be requested before OFF_COMPLETE timeout"
    );

    utest_systick_advance_time_ms(201);
    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        WBEC_PERIODIC_WAKEUP_FIRST_TIMEOUT_S,
        utest_mcu_get_standby_wakeup_time(),
        "Standby wakeup timeout must be requested after off timeout"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        MCU_VCC_5V_STATE_ON, mcu_get_vcc_5v_last_state(), "Saved 5V state must be ON when V50 is present"
    );
}

// Сценарий: граничное условие таймаута в PS_OFF_COMPLETE.
// До действия: система в OFF_COMPLETE после hard_off.
// После действия: в 200мс standby не запрашивается, в 201мс запрашивается.
static void test_periodic_off_complete_timeout_switches_to_standby_only_after_200ms(void)
{
    linux_cpu_pwr_seq_init(true);
    linux_cpu_pwr_seq_hard_off();
    prepare_periodic_runtime(true, false);

    utest_systick_advance_time_ms(200);
    linux_cpu_pwr_seq_do_periodic_work();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, utest_mcu_get_standby_wakeup_time(), "Standby must not be requested at exact 200ms in OFF_COMPLETE"
    );

    utest_systick_advance_time_ms(1);
    linux_cpu_pwr_seq_do_periodic_work();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        WBEC_PERIODIC_WAKEUP_FIRST_TIMEOUT_S,
        utest_mcu_get_standby_wakeup_time(),
        "Standby must be requested only after OFF_COMPLETE timeout is exceeded"
    );
}

// Сценарий: в состоянии OFF_COMPLETE включен stepup.
// После действия: periodic_work выключает stepup до перехода в standby.
static void test_periodic_off_complete_disables_stepup_when_enabled(void)
{
    linux_cpu_pwr_seq_init(true);
    linux_cpu_pwr_seq_hard_off();
    prepare_periodic_runtime(true, false);
    utest_set_wbmz_stepup_enabled(true);

    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(
        utest_wbmz_get_stepup_enabled(),
        "Stepup must be disabled in OFF_COMPLETE state"
    );
}

// Сценарий: неожиданно пропало 5V во время работы.
// До действия: standby не запрошен.
// После действия: немедленный переход в standby и сохранение статуса 5V=OFF.
static void test_periodic_v50_loss_goes_to_standby_with_saved_off_state(void)
{
    linux_cpu_pwr_seq_init(true);
    prepare_periodic_runtime(false, false);

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, utest_mcu_get_standby_wakeup_time(), "Standby must not be requested before V50 loss handling"
    );

    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        WBEC_PERIODIC_WAKEUP_FIRST_TIMEOUT_S,
        utest_mcu_get_standby_wakeup_time(),
        "Standby wakeup timeout must be requested on V50 loss"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        MCU_VCC_5V_STATE_OFF, mcu_get_vcc_5v_last_state(), "Saved 5V state must be OFF when V50 is absent"
    );
}

// ==================== Сценарии PMIC reset ====================

// Сценарий: вызов linux_cpu_pwr_seq_reset_pmic и пропадание 3.3V.
// До действия: RESET/PWRON отпущены.
// После действия: RESET и PWRON сбрасываются, модуль выходит из reset-состояния PMIC.
static void test_periodic_reset_pmic_completes_when_v33_is_lost(void)
{
    linux_cpu_pwr_seq_init(true);
    prepare_periodic_runtime(true, true);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_reset_gpio), "PMIC RESET GPIO must be low before reset_pmic"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be low before reset_pmic"
    );

    linux_cpu_pwr_seq_reset_pmic();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(pmic_reset_gpio), "PMIC RESET GPIO must be high right after reset_pmic"
    );

    utest_vmon_set_ch_status(VMON_CHANNEL_V33, false);
    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_reset_gpio), "PMIC RESET GPIO must be low after reset completion"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be low after reset completion"
    );
}

// Сценарий: вызов linux_cpu_pwr_seq_reset_pmic и сохранение 3.3V.
// До действия: RESET отпущен.
// После действия: по таймауту >2000мс RESET завершается даже при наличии 3.3V.
static void test_periodic_reset_pmic_completes_by_timeout(void)
{
    linux_cpu_pwr_seq_init(true);
    prepare_periodic_runtime(true, true);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0,
        utest_gpio_get_output_state(pmic_reset_gpio),
        "PMIC RESET GPIO must be low before reset_pmic timeout scenario"
    );

    linux_cpu_pwr_seq_reset_pmic();

    utest_systick_advance_time_ms(2000);
    linux_cpu_pwr_seq_do_periodic_work();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(pmic_reset_gpio), "PMIC RESET GPIO must stay high at exact timeout"
    );

    utest_systick_advance_time_ms(1);
    linux_cpu_pwr_seq_do_periodic_work();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_reset_gpio), "PMIC RESET GPIO must be low after timeout completion"
    );
}

// Сценарий: граничное условие таймаута в PS_RESET_PMIC_WAIT.
// До действия: reset PMIC активирован, 3.3V не пропадает.
// После действия: в 2000мс RESET не отпускается, в 2001мс reset завершается.
static void test_periodic_reset_pmic_timeout_switches_state_only_after_2000ms(void)
{
    linux_cpu_pwr_seq_init(true);
    prepare_periodic_runtime(true, true);

    linux_cpu_pwr_seq_reset_pmic();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(pmic_reset_gpio), "PMIC RESET GPIO must be high while waiting for reset timeout"
    );

    utest_systick_advance_time_ms(2000);
    linux_cpu_pwr_seq_do_periodic_work();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(pmic_reset_gpio), "PMIC RESET GPIO must stay high at exact 2000ms timeout"
    );

    utest_systick_advance_time_ms(1);
    linux_cpu_pwr_seq_do_periodic_work();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0,
        utest_gpio_get_output_state(pmic_reset_gpio),
        "PMIC RESET GPIO must go low only after 2000ms timeout is exceeded"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be low after reset timeout completion"
    );
}

// Сценарий: питание уже включено (ON_COMPLETE).
// До действия: после первого periodic_work состояние только переходит в ON_COMPLETE.
// После действия: последующие вызовы periodic_work вызывают wbmz_do_periodic_work.
static void test_periodic_on_complete_calls_wbmz_periodic_work(void)
{
    linux_cpu_pwr_seq_init(true);
    prepare_periodic_runtime(true, true);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0,
        utest_get_wbmz_periodic_work_call_count(),
        "wbmz_do_periodic_work call count must be zero before periodic work"
    );

    linux_cpu_pwr_seq_do_periodic_work();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0,
        utest_get_wbmz_periodic_work_call_count(),
        "First periodic work call should only transition to ON_COMPLETE"
    );
    linux_cpu_pwr_seq_do_periodic_work();
    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        2,
        utest_get_wbmz_periodic_work_call_count(),
        "wbmz_do_periodic_work must be called in ON_COMPLETE state"
    );
}

// Сценарий: зафиксировано долгое нажатие кнопки питания.
// До действия: питание включено, stepup активирован.
// После действия: питание выключается, stepup и LED отключаются, запрашивается переход в standby.
static void test_periodic_long_press_turns_power_off_and_goes_to_standby(void)
{
    linux_cpu_pwr_seq_init(true);
    prepare_periodic_runtime(true, true);

    utest_set_wbmz_stepup_enabled(true);
    utest_set_pwrkey_long_press(true);
    utest_set_pwrkey_pressed(false);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(linux_power_gpio), "Linux power GPIO must be high before long-press handling"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must be low before long-press handling"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, utest_mcu_get_standby_wakeup_time(), "Standby must not be requested before long-press handling"
    );

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0,
        utest_pwrkey_get_periodic_work_call_count(),
        "Button wait loop must not run before long-press handling"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        MCU_VCC_5V_STATE_OFF,
        mcu_get_vcc_5v_last_state(),
        "Saved 5V state must have default OFF before long-press handling"
    );

    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(linux_power_gpio), "Linux power GPIO must be low after long-press shutdown"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        WBEC_PERIODIC_WAKEUP_FIRST_TIMEOUT_S,
        utest_mcu_get_standby_wakeup_time(),
        "Standby wakeup timeout must be requested on long press"
    );
    TEST_ASSERT_FALSE_MESSAGE(
        utest_wbmz_get_stepup_enabled(),
        "Stepup must be disabled on long press shutdown path"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0,
        utest_pwrkey_get_periodic_work_call_count(),
        "Button wait loop must not run when button is already released"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_pwron_gpio), "PMIC PWRON GPIO must remain low on long press shutdown path"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        MCU_VCC_5V_STATE_ON,
        mcu_get_vcc_5v_last_state(),
        "Saved 5V state must be ON when V50 is present on long press shutdown"
    );
}

// Сценарий: долгое нажатие при удержании кнопки.
// До действия: watchdog еще не перезагружался, счетчик periodic_work по кнопке нулевой.
// После действия: выполняется цикл ожидания с pwrkey_do_periodic_work и watchdog_reload.
static void test_periodic_long_press_waits_until_button_release(void)
{
    linux_cpu_pwr_seq_init(true);
    prepare_periodic_runtime(true, true);
    utest_set_pwrkey_long_press(true);
    utest_set_pwrkey_pressed(true);
    release_pwrkey_from_watchdog = true;

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0,
        utest_pwrkey_get_periodic_work_call_count(),
        "pwrkey_do_periodic_work call count must be zero before long-press loop"
    );
    TEST_ASSERT_FALSE_MESSAGE(
        utest_watchdog_is_reloaded(), "Watchdog must not be reloaded before long-press loop starts"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, utest_mcu_get_standby_wakeup_time(), "Standby must not be requested before long-press loop starts"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(linux_power_gpio), "Linux power GPIO must be high before long-press loop starts"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        MCU_VCC_5V_STATE_OFF,
        mcu_get_vcc_5v_last_state(),
        "Saved 5V state must have default OFF before long-press loop starts"
    );

    linux_cpu_pwr_seq_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(
        utest_watchdog_is_reloaded(), "Watchdog must be reloaded while waiting for button release"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1,
        utest_pwrkey_get_periodic_work_call_count(),
        "pwrkey_do_periodic_work must be called while waiting for release"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(linux_power_gpio), "Linux power GPIO must be low after long-press handling"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        WBEC_PERIODIC_WAKEUP_FIRST_TIMEOUT_S,
        utest_mcu_get_standby_wakeup_time(),
        "Standby must be requested after long-press loop finishes"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        MCU_VCC_5V_STATE_ON,
        mcu_get_vcc_5v_last_state(),
        "Saved 5V state must become ON when V50 is present during long press"
    );
}

// Сценарий: standalone вызов reset_pmic.
// До действия: RESET-линия опущена.
// После действия: RESET-линия поднимается сразу.
static void test_reset_pmic_sets_reset_line_high(void)
{
    linux_cpu_pwr_seq_init(true);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, utest_gpio_get_output_state(pmic_reset_gpio), "PMIC RESET GPIO must be low before reset_pmic"
    );

    linux_cpu_pwr_seq_reset_pmic();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1, utest_gpio_get_output_state(pmic_reset_gpio), "PMIC RESET GPIO must be high right after reset_pmic"
    );
}

// Сценарий: прямой вызов linux_cpu_pwr_seq_off_and_goto_standby.
// До действия: регистры PWR и параметр wakeup не установлены.
// После действия: выставляется pull-down для Linux power GPIO, включается APC и передается timeout в standby.
static void test_off_and_goto_standby_sets_pwr_registers_and_wakeup_timeout(void)
{
    const uint16_t wakeup_timeout_s = 123;
    const uint32_t linux_power_pd_mask = (1UL << linux_power_gpio.pin);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, PWR->PDCRD, "PWR->PDCRD must be zero before off_and_goto_standby");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, PWR->CR3, "PWR->CR3 must be zero before off_and_goto_standby");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, utest_mcu_get_standby_wakeup_time(), "Standby wakeup time must be zero before off_and_goto_standby"
    );

    linux_cpu_pwr_seq_off_and_goto_standby(wakeup_timeout_s);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, (PWR->PDCRD & linux_power_pd_mask), "PDCRD bit for Linux power GPIO must be set");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, (PWR->CR3 & PWR_CR3_APC), "PWR_CR3_APC bit must be set in CR3");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        wakeup_timeout_s,
        utest_mcu_get_standby_wakeup_time(),
        "Standby wakeup timeout must be forwarded to mcu_goto_standby"
    );
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_periodic_init_off_state_does_not_change_outputs);
    RUN_TEST(test_periodic_returns_early_when_module_not_initialized);
    RUN_TEST(test_periodic_returns_early_when_vmon_is_not_ready);
    RUN_TEST(test_periodic_step1_timeout_changes_state_only_after_1000ms);
    RUN_TEST(test_periodic_step2_timeout_changes_state_only_after_1500ms);
    RUN_TEST(test_periodic_power_on_success_when_v33_is_present);
    RUN_TEST(test_periodic_power_on_fallback_enables_pmic_pwron);
    RUN_TEST(test_periodic_power_on_fallback_completes_when_v33_appears);

    RUN_TEST(test_linux_cpu_pwr_seq_on_returns_early_when_sequence_is_already_running);
    RUN_TEST(test_linux_cpu_pwr_seq_on_returns_early_in_step1_without_timer_reset);
    RUN_TEST(test_linux_cpu_pwr_seq_on_returns_early_in_step3_without_timer_reset);
    RUN_TEST(test_linux_cpu_pwr_seq_on_returns_early_in_on_complete_state);

    RUN_TEST(test_is_busy_returns_false_in_off_complete_state);
    RUN_TEST(test_is_busy_returns_false_in_on_complete_state);
    RUN_TEST(test_periodic_power_on_fallback_exhaustion_drops_5v);
    RUN_TEST(test_periodic_step3_retry_happens_only_after_timeout_is_strictly_exceeded);
    RUN_TEST(test_periodic_hard_reset_recovery_reenables_5v_after_timeout);
    RUN_TEST(test_periodic_hard_off_timeout_goes_to_standby_with_v50_on);
    RUN_TEST(test_periodic_off_complete_timeout_switches_to_standby_only_after_200ms);
    RUN_TEST(test_periodic_off_complete_disables_stepup_when_enabled);
    RUN_TEST(test_periodic_v50_loss_goes_to_standby_with_saved_off_state);

    RUN_TEST(test_periodic_reset_pmic_completes_when_v33_is_lost);
    RUN_TEST(test_periodic_reset_pmic_completes_by_timeout);
    RUN_TEST(test_periodic_reset_pmic_timeout_switches_state_only_after_2000ms);
    RUN_TEST(test_periodic_on_complete_calls_wbmz_periodic_work);
    RUN_TEST(test_periodic_long_press_turns_power_off_and_goes_to_standby);
    RUN_TEST(test_periodic_long_press_waits_until_button_release);
    RUN_TEST(test_reset_pmic_sets_reset_line_high);
    RUN_TEST(test_off_and_goto_standby_sets_pwr_registers_and_wakeup_timeout);

    return UNITY_END();
}
