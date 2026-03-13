#include "unity.h"
#include "wbmz-common.h"
#include "utest_gpio.h"
#include "utest_voltage_monitor.h"
#include "utest_systick.h"
#include "config.h"

#define LOG_LEVEL LOG_LEVEL_INFO
#include "console_log.h"

static const gpio_pin_t stepup_enable_gpio = { EC_GPIO_WBMZ_STEPUP_ENABLE };
static const gpio_pin_t status_bat_gpio = { EC_GPIO_WBMZ_STATUS_BAT };

#if defined WBEC_GPIO_WBMZ_CHARGE_ENABLE
    static const gpio_pin_t charge_enable_gpio = { WBEC_GPIO_WBMZ_CHARGE_ENABLE };
#endif

void utest_wbmz_common_reset_state(void);

void setUp(void)
{
    // Reset all GPIO and mock states
    utest_gpio_reset_instances();
    utest_systick_set_time_ms(0);
    vmon_init();

    // Reset wbmz-common state
    utest_wbmz_common_reset_state();

    // Set default voltage monitor states (all channels OK)
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN_FOR_WBMZ, true);
    utest_vmon_set_ch_status(VMON_CHANNEL_VBUS_DEBUG, false);

    #if !defined(EC_USB_HUB_DEBUG_NETWORK)
        utest_vmon_set_ch_status(VMON_CHANNEL_VBUS_NETWORK, false);
    #endif

    #if defined WBEC_GPIO_WBMZ_CHARGE_ENABLE
        utest_vmon_set_ch_status(VMON_CHANNEL_VBAT, true);
    #endif
}

void tearDown(void)
{
}


// ============================================================================
// Тесты инициализации
// ============================================================================

// Сценарий: вызов wbmz_init()
// Ожидается: все GPIO правильно сконфигурированы, stepup и charging выключены
static void test_wbmz_init(void)
{
    LOG_INFO("Testing WBMZ initialization");

    wbmz_init();

    uint32_t mode = utest_gpio_get_mode(stepup_enable_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        GPIO_MODE_OUTPUT, mode,
        "Stepup enable GPIO should be configured as OUTPUT"
    );

    uint32_t otype = utest_gpio_get_output_type(stepup_enable_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        GPIO_OTYPE_PUSH_PULL, otype,
        "Stepup enable GPIO should be PUSH-PULL"
    );

    uint32_t state = utest_gpio_get_output_state(stepup_enable_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, state,
        "Stepup enable should be LOW (disabled) after init"
    );

    mode = utest_gpio_get_mode(status_bat_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        GPIO_MODE_INPUT, mode,
        "STATUS_BAT GPIO should be configured as INPUT"
    );

    bool enabled = wbmz_is_stepup_enabled();
    TEST_ASSERT_FALSE_MESSAGE(enabled, "Stepup should be disabled after initialization");

    #if defined WBEC_GPIO_WBMZ_CHARGE_ENABLE
        mode = utest_gpio_get_mode(charge_enable_gpio);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(
            GPIO_MODE_OUTPUT, mode,
            "Charge enable GPIO should be configured as OUTPUT"
        );

        otype = utest_gpio_get_output_type(charge_enable_gpio);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(
            GPIO_OTYPE_PUSH_PULL, otype,
            "Charge enable GPIO should be PUSH-PULL"
        );

        state = utest_gpio_get_output_state(charge_enable_gpio);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(
            0, state,
            "Charge enable should be LOW (disabled) after init"
        );

        enabled = wbmz_is_charging_enabled();
        TEST_ASSERT_FALSE_MESSAGE(enabled, "Charging should be disabled after initialization");
    #endif
}


// ============================================================================
// Тесты wbmz_is_powered_from_wbmz() - считывание GPIO STATUS_BAT
// ============================================================================

// Сценарий: STATUS_BAT установлен в LOW (WBMZ активен, open collector)
// Ожидается: wbmz_is_powered_from_wbmz() возвращает true
static void test_powered_from_wbmz_when_gpio_low(void)
{
    LOG_INFO("Testing powered_from_wbmz when STATUS_BAT is LOW");

    wbmz_init();
    utest_gpio_set_input_state(status_bat_gpio, 0);

    bool powered = wbmz_is_powered_from_wbmz();
    TEST_ASSERT_TRUE_MESSAGE(powered, "Should be powered from WBMZ when STATUS_BAT GPIO is LOW");
}


// Сценарий: STATUS_BAT установлен в HIGH (WBMZ неактивен, подтянут к V_EC)
// Ожидается: wbmz_is_powered_from_wbmz() возвращает false
static void test_not_powered_from_wbmz_when_gpio_high(void)
{
    LOG_INFO("Testing NOT powered_from_wbmz when STATUS_BAT is HIGH");

    wbmz_init();
    utest_gpio_set_input_state(status_bat_gpio, 1);

    bool powered = wbmz_is_powered_from_wbmz();
    TEST_ASSERT_FALSE_MESSAGE(powered, "Should NOT be powered from WBMZ when STATUS_BAT GPIO is HIGH");
}


// ============================================================================
// Тесты прямого управления stepup
// ============================================================================

// Сценарий: вызов wbmz_enable_stepup(), затем wbmz_disable_stepup()
// Ожидается: GPIO и флаг состояния корректно переключаются между HIGH/LOW
static void test_stepup_enable_disable(void)
{
    LOG_INFO("Testing stepup enable and disable");

    wbmz_init();
    wbmz_enable_stepup();

    uint32_t state = utest_gpio_get_output_state(stepup_enable_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Stepup GPIO should be HIGH when enabled");

    bool enabled = wbmz_is_stepup_enabled();
    TEST_ASSERT_TRUE_MESSAGE(enabled, "is_stepup_enabled() should return true");

    wbmz_disable_stepup();

    state = utest_gpio_get_output_state(stepup_enable_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Stepup GPIO should be LOW when disabled");

    enabled = wbmz_is_stepup_enabled();
    TEST_ASSERT_FALSE_MESSAGE(enabled, "is_stepup_enabled() should return false");
}


// ============================================================================
// Тесты автоматического управления stepup
// ============================================================================

// Сценарий: Vin OK, USB нет, вызов wbmz_do_periodic_work()
// Ожидается: stepup автоматически включается
static void test_stepup_auto_enable_when_vin_ok_and_no_usb(void)
{
    LOG_INFO("Testing automatic stepup enable: Vin OK, no USB");

    wbmz_init();

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN_FOR_WBMZ, true);
    utest_vmon_set_ch_status(VMON_CHANNEL_VBUS_DEBUG, false);
    #if !defined(EC_USB_HUB_DEBUG_NETWORK)
        utest_vmon_set_ch_status(VMON_CHANNEL_VBUS_NETWORK, false);
    #endif

    wbmz_do_periodic_work();

    bool enabled = wbmz_is_stepup_enabled();
    TEST_ASSERT_TRUE_MESSAGE(enabled, "Stepup should be enabled when Vin OK and no USB");

    uint32_t state = utest_gpio_get_output_state(stepup_enable_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Stepup GPIO should be HIGH when auto-enabled");
}


// Сценарий: Vin OK, но подключен USB DEBUG
// Ожидается: stepup НЕ включается (чтобы не разряжать батарею)
static void test_stepup_not_enabled_when_usb_present(void)
{
    LOG_INFO("Testing stepup NOT enabled when USB is present");

    wbmz_init();

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN_FOR_WBMZ, true);
    utest_vmon_set_ch_status(VMON_CHANNEL_VBUS_DEBUG, true);

    wbmz_do_periodic_work();

    bool enabled = wbmz_is_stepup_enabled();
    TEST_ASSERT_FALSE_MESSAGE(enabled, "Stepup should NOT be enabled when USB is present");
}


#if !defined(EC_USB_HUB_DEBUG_NETWORK)
// Сценарий: Vin OK, но подключен USB NETWORK
// Ожидается: stepup НЕ включается
static void test_stepup_not_enabled_when_usb_network_present(void)
{
    LOG_INFO("Testing stepup NOT enabled when USB NETWORK is present");

    wbmz_init();

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN_FOR_WBMZ, true);
    utest_vmon_set_ch_status(VMON_CHANNEL_VBUS_DEBUG, false);
    utest_vmon_set_ch_status(VMON_CHANNEL_VBUS_NETWORK, true);

    wbmz_do_periodic_work();

    bool enabled = wbmz_is_stepup_enabled();
    TEST_ASSERT_FALSE_MESSAGE(enabled, "Stepup should NOT be enabled when USB NETWORK is present");
}
#endif


// Сценарий: Vin NOT OK, USB нет
// Ожидается: stepup НЕ включается (недостаточное Vin)
static void test_stepup_not_enabled_when_vin_not_ok(void)
{
    LOG_INFO("Testing stepup NOT enabled when Vin not OK");

    wbmz_init();

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN_FOR_WBMZ, false);
    utest_vmon_set_ch_status(VMON_CHANNEL_VBUS_DEBUG, false);

    wbmz_do_periodic_work();

    bool enabled = wbmz_is_stepup_enabled();
    TEST_ASSERT_FALSE_MESSAGE(enabled, "Stepup should NOT be enabled when Vin is not OK");
}


// Сценарий: stepup включен, затем батарея разрядилась (Vin=LOW, STATUS_BAT=HIGH)
// Ожидается: stepup выключается только после превышения порога фильтра 500ms
static void test_stepup_auto_disable_when_battery_discharged(void)
{
    LOG_INFO("Testing automatic stepup disable when battery discharged");

    wbmz_init();

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN_FOR_WBMZ, true);
    utest_vmon_set_ch_status(VMON_CHANNEL_VBUS_DEBUG, false);
    wbmz_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wbmz_is_stepup_enabled(), "Stepup should be enabled initially");

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN_FOR_WBMZ, false);
    utest_gpio_set_input_state(status_bat_gpio, 1);

    utest_systick_advance_time_ms(500);
    wbmz_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(
        wbmz_is_stepup_enabled(),
        "Stepup should still be enabled at exactly 500ms (filter threshold)"
    );

    utest_systick_advance_time_ms(1);
    wbmz_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(
        wbmz_is_stepup_enabled(),
        "Stepup should be disabled after exceeding 500ms filter threshold"
    );

    uint32_t state = utest_gpio_get_output_state(stepup_enable_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Stepup GPIO should be LOW when auto-disabled");
}


// Сценарий: stepup включен, затем батарея разрядилась, но условие разряда исчезло до истечения фильтра
// Ожидается: фильтр сбрасывается, stepup остается включенным
static void test_stepup_discharge_filter_reset(void)
{
    LOG_INFO("Testing stepup discharge filter reset");

    wbmz_init();

    // Включаем stepup
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN_FOR_WBMZ, true);
    utest_vmon_set_ch_status(VMON_CHANNEL_VBUS_DEBUG, false);
    wbmz_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wbmz_is_stepup_enabled(), "Stepup should be enabled initially");

    // Создаем условие разряда
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN_FOR_WBMZ, false);
    utest_gpio_set_input_state(status_bat_gpio, 1);

    // Ждем 400ms (меньше порога 500ms)
    utest_systick_advance_time_ms(400);
    wbmz_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(
        wbmz_is_stepup_enabled(),
        "Stepup should still be enabled at 400ms"
    );

    // Условие разряда исчезает (Vin восстанавливается)
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN_FOR_WBMZ, true);
    wbmz_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(
        wbmz_is_stepup_enabled(),
        "Stepup should stay enabled when Vin recovers"
    );

    // Снова создаем условие разряда
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN_FOR_WBMZ, false);
    utest_systick_advance_time_ms(100);
    wbmz_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(
        wbmz_is_stepup_enabled(),
        "Stepup should still be enabled - filter should have been reset"
    );

    // Теперь ждем полные 500ms с момента последнего условия разряда
    utest_systick_advance_time_ms(401);
    wbmz_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(
        wbmz_is_stepup_enabled(),
        "Stepup should be disabled after exceeding 500ms from filter reset"
    );
}


// Сценарий: stepup включен, Vin упал, но STATUS_BAT=LOW (питаемся от WBMZ)
// Ожидается: stepup ОСТАЕТСЯ включенным (работаем от батареи)
static void test_stepup_stays_enabled_when_powered_from_wbmz(void)
{
    LOG_INFO("Testing stepup stays enabled when powered from WBMZ");

    wbmz_init();

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN_FOR_WBMZ, true);
    wbmz_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wbmz_is_stepup_enabled(), "Stepup should be enabled initially");

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN_FOR_WBMZ, false);
    utest_gpio_set_input_state(status_bat_gpio, 0);

    utest_systick_advance_time_ms(600);
    wbmz_do_periodic_work();

    bool enabled = wbmz_is_stepup_enabled();
    TEST_ASSERT_TRUE_MESSAGE(enabled, "Stepup should stay enabled when powered from WBMZ");
}


// Сценарий: stepup включен автоматикой, затем подключается USB
// Ожидается: stepup ОСТАЕТСЯ включенным (проверка логики из комментария:
// "если хотим чтоб WBMZ работал при подключенном USB, надо сначала включить контроллер кнопкой, а потом подключать USB")
static void test_stepup_stays_enabled_when_usb_connected(void)
{
    LOG_INFO("Testing stepup stays enabled when USB connected after stepup enabled");

    wbmz_init();

    // Включаем stepup автоматикой (Vin OK, no USB)
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN_FOR_WBMZ, true);
    utest_vmon_set_ch_status(VMON_CHANNEL_VBUS_DEBUG, false);
    #if !defined(EC_USB_HUB_DEBUG_NETWORK)
        utest_vmon_set_ch_status(VMON_CHANNEL_VBUS_NETWORK, false);
    #endif
    wbmz_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wbmz_is_stepup_enabled(), "Stepup should be enabled initially");

    // Подключаем USB DEBUG
    utest_vmon_set_ch_status(VMON_CHANNEL_VBUS_DEBUG, true);
    wbmz_do_periodic_work();

    // Stepup должен ОСТАТЬСЯ включенным (USB проверяется только при включении в блоке else)
    bool enabled = wbmz_is_stepup_enabled();
    TEST_ASSERT_TRUE_MESSAGE(
        enabled,
        "Stepup should stay enabled when USB connected after stepup was already enabled"
    );

    uint32_t state = utest_gpio_get_output_state(stepup_enable_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Stepup GPIO should stay HIGH");

    #if !defined(EC_USB_HUB_DEBUG_NETWORK)
        // Проверяем также с USB NETWORK
        utest_vmon_set_ch_status(VMON_CHANNEL_VBUS_DEBUG, false);
        utest_vmon_set_ch_status(VMON_CHANNEL_VBUS_NETWORK, true);
        wbmz_do_periodic_work();

        enabled = wbmz_is_stepup_enabled();
        TEST_ASSERT_TRUE_MESSAGE(
            enabled,
            "Stepup should stay enabled when USB NETWORK connected after stepup was already enabled"
        );
    #endif
}


// ============================================================================
// Тесты force control для stepup
// ============================================================================

// Сценарий: включен force control с состоянием ON, условия для авто-выключения
// Ожидается: stepup включен несмотря на автоматику
static void test_stepup_force_control_enable(void)
{
    LOG_INFO("Testing stepup force control - enable");

    wbmz_init();

    wbmz_set_stepup_force_control(true, true);

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN_FOR_WBMZ, false);
    wbmz_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(
        wbmz_is_stepup_enabled(),
        "Stepup should be enabled by force control, ignoring automatic logic"
    );

    uint32_t state = utest_gpio_get_output_state(stepup_enable_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Stepup GPIO should be HIGH when force enabled");
}


// Сценарий: stepup включен, затем force control с состоянием OFF, условия для авто-включения
// Ожидается: stepup выключен несмотря на автоматику
static void test_stepup_force_control_disable(void)
{
    LOG_INFO("Testing stepup force control - disable");

    wbmz_init();

    wbmz_enable_stepup();

    wbmz_set_stepup_force_control(true, false);

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN_FOR_WBMZ, true);
    wbmz_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(
        wbmz_is_stepup_enabled(),
        "Stepup should be disabled by force control, ignoring automatic logic"
    );

    uint32_t state = utest_gpio_get_output_state(stepup_enable_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Stepup GPIO should be LOW when force disabled");
}


// Сценарий: force control включен, затем отключен
// Ожидается: stepup выключается, затем автоматика снова работает
static void test_stepup_force_control_release(void)
{
    LOG_INFO("Testing stepup force control release");

    wbmz_init();

    wbmz_set_stepup_force_control(true, true);
    wbmz_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wbmz_is_stepup_enabled(), "Stepup should be enabled by force control");

    wbmz_set_stepup_force_control(false, false);

    bool enabled = wbmz_is_stepup_enabled();
    TEST_ASSERT_FALSE_MESSAGE(enabled, "Stepup should be disabled when force control is released");

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN_FOR_WBMZ, true);
    utest_vmon_set_ch_status(VMON_CHANNEL_VBUS_DEBUG, false);
    wbmz_do_periodic_work();

    enabled = wbmz_is_stepup_enabled();
    TEST_ASSERT_TRUE_MESSAGE(enabled, "Stepup should be auto-enabled after force control released");
}


// Сценарий: прямое переключение force control из ON в OFF без release
// Ожидается: stepup корректно выключается
static void test_stepup_force_control_switch_on_to_off(void)
{
    LOG_INFO("Testing stepup force control switch from ON to OFF");

    wbmz_init();

    // Включаем force control с состоянием ON
    wbmz_set_stepup_force_control(true, true);
    wbmz_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wbmz_is_stepup_enabled(), "Stepup should be enabled by force control ON");

    uint32_t state = utest_gpio_get_output_state(stepup_enable_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Stepup GPIO should be HIGH");

    // Переключаем force control на OFF (без вызова release)
    wbmz_set_stepup_force_control(true, false);
    wbmz_do_periodic_work();

    bool enabled = wbmz_is_stepup_enabled();
    TEST_ASSERT_FALSE_MESSAGE(
        enabled,
        "Stepup should be disabled when force control switched to OFF"
    );

    state = utest_gpio_get_output_state(stepup_enable_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Stepup GPIO should be LOW");

    // Проверяем что force control все еще активен (автоматика не работает)
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN_FOR_WBMZ, true);
    utest_vmon_set_ch_status(VMON_CHANNEL_VBUS_DEBUG, false);
    wbmz_do_periodic_work();

    enabled = wbmz_is_stepup_enabled();
    TEST_ASSERT_FALSE_MESSAGE(
        enabled,
        "Stepup should stay disabled - force control is still active"
    );

    // Переключаем обратно на ON
    wbmz_set_stepup_force_control(true, true);
    wbmz_do_periodic_work();

    enabled = wbmz_is_stepup_enabled();
    TEST_ASSERT_TRUE_MESSAGE(
        enabled,
        "Stepup should be enabled when force control switched back to ON"
    );
}


// ============================================================================
// Тесты управления зарядом (только для WB85 с WBEC_GPIO_WBMZ_CHARGE_ENABLE)
// ============================================================================

#if defined WBEC_GPIO_WBMZ_CHARGE_ENABLE

// Сценарий: Vin OK, не питаемся от WBMZ (STATUS_BAT=HIGH)
// Ожидается: заряд автоматически включается
static void test_charging_auto_enable_when_vin_ok_and_not_powered_from_wbmz(void)
{
    LOG_INFO("Testing automatic charging enable: Vin OK, not powered from WBMZ");

    wbmz_init();

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);
    utest_gpio_set_input_state(status_bat_gpio, 1);

    wbmz_do_periodic_work();

    bool enabled = wbmz_is_charging_enabled();
    TEST_ASSERT_TRUE_MESSAGE(enabled, "Charging should be enabled when Vin OK and not powered from WBMZ");

    uint32_t state = utest_gpio_get_output_state(charge_enable_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Charge enable GPIO should be HIGH");
}


// Сценарий: заряд включен, затем Vin пропал
// Ожидается: заряд автоматически выключается
static void test_charging_auto_disable_when_vin_not_ok(void)
{
    LOG_INFO("Testing automatic charging disable when Vin not OK");

    wbmz_init();

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);
    utest_gpio_set_input_state(status_bat_gpio, 1);
    wbmz_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wbmz_is_charging_enabled(), "Charging should be enabled initially");

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, false);
    wbmz_do_periodic_work();

    bool enabled = wbmz_is_charging_enabled();
    TEST_ASSERT_FALSE_MESSAGE(enabled, "Charging should be disabled when Vin is not OK");

    uint32_t state = utest_gpio_get_output_state(charge_enable_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Charge enable GPIO should be LOW when disabled");
}


// Сценарий: заряд включен, затем начали питаться от WBMZ (STATUS_BAT=LOW)
// Ожидается: заряд автоматически выключается
static void test_charging_auto_disable_when_powered_from_wbmz(void)
{
    LOG_INFO("Testing automatic charging disable when powered from WBMZ");

    wbmz_init();

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);
    utest_gpio_set_input_state(status_bat_gpio, 1);
    wbmz_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wbmz_is_charging_enabled(), "Charging should be enabled initially");

    utest_gpio_set_input_state(status_bat_gpio, 0);
    wbmz_do_periodic_work();

    bool enabled = wbmz_is_charging_enabled();
    TEST_ASSERT_FALSE_MESSAGE(enabled, "Charging should be disabled when powered from WBMZ");
}


// Сценарий: включен force control с состоянием ON, условия для авто-выключения
// Ожидается: заряд включен несмотря на автоматику
static void test_charging_force_control_enable(void)
{
    LOG_INFO("Testing charging force control - enable");

    wbmz_init();

    wbmz_set_charging_force_control(true, true);

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, false);
    wbmz_do_periodic_work();

    bool enabled = wbmz_is_charging_enabled();
    TEST_ASSERT_TRUE_MESSAGE(enabled, "Charging should be enabled by force control, ignoring automatic logic");

    uint32_t state = utest_gpio_get_output_state(charge_enable_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Charge enable GPIO should be HIGH when force enabled");
}


// Сценарий: заряд выключен, Vin NOT OK, вызов wbmz_do_periodic_work()
// Ожидается: заряд остается выключенным
static void test_charging_stays_disabled_when_vin_not_ok(void)
{
    LOG_INFO("Testing charging stays disabled when Vin not OK");

    wbmz_init();

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, false);
    utest_gpio_set_input_state(status_bat_gpio, 1);
    wbmz_do_periodic_work();

    bool enabled = wbmz_is_charging_enabled();
    TEST_ASSERT_FALSE_MESSAGE(enabled, "Charging should stay disabled when Vin is not OK");
}


// Сценарий: включен force control с состоянием OFF, условия для авто-включения
// Ожидается: заряд выключен несмотря на автоматику
static void test_charging_force_control_disable(void)
{
    LOG_INFO("Testing charging force control - disable");

    wbmz_init();

    wbmz_set_charging_force_control(true, false);

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);
    utest_gpio_set_input_state(status_bat_gpio, 1);
    wbmz_do_periodic_work();

    bool enabled = wbmz_is_charging_enabled();
    TEST_ASSERT_FALSE_MESSAGE(enabled, "Charging should be disabled by force control, ignoring automatic logic");

    uint32_t state = utest_gpio_get_output_state(charge_enable_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Charge enable GPIO should be LOW when force disabled");
}


// Сценарий: заряд включен автоматикой, затем включен force control OFF
// Ожидается: заряд выключается force control'ом
static void test_charging_force_control_disable_when_enabled(void)
{
    LOG_INFO("Testing charging force control disable when already enabled");

    wbmz_init();

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);
    utest_gpio_set_input_state(status_bat_gpio, 1);
    wbmz_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wbmz_is_charging_enabled(), "Charging should be enabled initially");

    wbmz_set_charging_force_control(true, false);
    wbmz_do_periodic_work();

    bool enabled = wbmz_is_charging_enabled();
    TEST_ASSERT_FALSE_MESSAGE(enabled, "Charging should be disabled by force control");

    uint32_t state = utest_gpio_get_output_state(charge_enable_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Charge enable GPIO should be LOW when force disabled");
}


// Сценарий: force control включен, затем отключен
// Ожидается: заряд выключается, затем автоматика снова работает
static void test_charging_force_control_release(void)
{
    LOG_INFO("Testing charging force control release");

    wbmz_init();

    wbmz_set_charging_force_control(true, true);
    wbmz_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wbmz_is_charging_enabled(), "Charging should be enabled by force control");

    wbmz_set_charging_force_control(false, false);

    bool enabled = wbmz_is_charging_enabled();
    TEST_ASSERT_FALSE_MESSAGE(enabled, "Charging should be disabled when force control is released");

    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);
    utest_gpio_set_input_state(status_bat_gpio, 1);
    wbmz_do_periodic_work();

    enabled = wbmz_is_charging_enabled();
    TEST_ASSERT_TRUE_MESSAGE(enabled, "Charging should be auto-enabled after force control released");
}


// Сценарий: проверка статуса VBAT через voltage monitor
// Ожидается: wbmz_is_vbat_ok() корректно отражает состояние VMON_CHANNEL_VBAT
static void test_vbat_ok_status(void)
{
    LOG_INFO("Testing VBAT OK status");

    wbmz_init();

    utest_vmon_set_ch_status(VMON_CHANNEL_VBAT, true);
    bool vbat_ok = wbmz_is_vbat_ok();
    TEST_ASSERT_TRUE_MESSAGE(vbat_ok, "VBAT should be OK");

    utest_vmon_set_ch_status(VMON_CHANNEL_VBAT, false);
    vbat_ok = wbmz_is_vbat_ok();
    TEST_ASSERT_FALSE_MESSAGE(vbat_ok, "VBAT should NOT be OK");
}


// ============================================================================
// Тесты стабильности force control при множественных вызовах periodic_work
// ============================================================================

// Сценарий: stepup включен через force control ON, условия для авто-выключения, повторные вызовы periodic_work
// Ожидается: stepup остается включенным, GPIO не меняется (автоматика игнорируется)
static void test_stepup_force_control_stability_enabled(void)
{
    LOG_INFO("Testing stepup force control stability - enabled state");

    wbmz_init();

    wbmz_set_stepup_force_control(true, true);

    // Создаем условия для авто-выключения (Vin NOT OK)
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN_FOR_WBMZ, false);

    // Множественные вызовы periodic_work - состояние не должно меняться
    for (int i = 0; i < 10; i++) {
        wbmz_do_periodic_work();
        TEST_ASSERT_TRUE_MESSAGE(wbmz_is_stepup_enabled(), "Stepup should stay enabled by force control");
        uint32_t state = utest_gpio_get_output_state(stepup_enable_gpio);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Stepup GPIO should stay HIGH");
    }
}


// Сценарий: stepup выключен через force control OFF, условия для авто-включения, повторные вызовы periodic_work
// Ожидается: stepup остается выключенным, GPIO не меняется (автоматика игнорируется)
static void test_stepup_force_control_stability_disabled(void)
{
    LOG_INFO("Testing stepup force control stability - disabled state");

    wbmz_init();

    wbmz_set_stepup_force_control(true, false);

    // Создаем условия для авто-включения (Vin OK, USB нет)
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN_FOR_WBMZ, true);
    utest_vmon_set_ch_status(VMON_CHANNEL_VBUS_DEBUG, false);
    #if !defined(EC_USB_HUB_DEBUG_NETWORK)
        utest_vmon_set_ch_status(VMON_CHANNEL_VBUS_NETWORK, false);
    #endif

    // Множественные вызовы periodic_work - состояние не должно меняться
    for (int i = 0; i < 10; i++) {
        wbmz_do_periodic_work();
        TEST_ASSERT_FALSE_MESSAGE(wbmz_is_stepup_enabled(), "Stepup should stay disabled by force control");
        uint32_t state = utest_gpio_get_output_state(stepup_enable_gpio);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Stepup GPIO should stay LOW");
    }
}


// Сценарий: заряд включен через force control ON, условия для авто-выключения, повторные вызовы periodic_work
// Ожидается: заряд остается включенным, GPIO не меняется (автоматика игнорируется)
static void test_charging_force_control_stability_enabled(void)
{
    LOG_INFO("Testing charging force control stability - enabled state");

    wbmz_init();

    wbmz_set_charging_force_control(true, true);

    // Создаем условия для авто-выключения (Vin NOT OK)
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, false);

    // Множественные вызовы periodic_work - состояние не должно меняться
    for (int i = 0; i < 10; i++) {
        wbmz_do_periodic_work();
        TEST_ASSERT_TRUE_MESSAGE(wbmz_is_charging_enabled(), "Charging should stay enabled by force control");
        uint32_t state = utest_gpio_get_output_state(charge_enable_gpio);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Charge enable GPIO should stay HIGH");
    }
}


// Сценарий: заряд выключен через force control OFF, условия для авто-включения, повторные вызовы periodic_work
// Ожидается: заряд остается выключенным, GPIO не меняется (автоматика игнорируется)
static void test_charging_force_control_stability_disabled(void)
{
    LOG_INFO("Testing charging force control stability - disabled state");

    wbmz_init();

    wbmz_set_charging_force_control(true, false);

    // Создаем условия для авто-включения (Vin OK, не питаемся от WBMZ)
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);
    utest_gpio_set_input_state(status_bat_gpio, 1);

    // Множественные вызовы periodic_work - состояние не должно меняться
    for (int i = 0; i < 10; i++) {
        wbmz_do_periodic_work();
        TEST_ASSERT_FALSE_MESSAGE(wbmz_is_charging_enabled(), "Charging should stay disabled by force control");
        uint32_t state = utest_gpio_get_output_state(charge_enable_gpio);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Charge enable GPIO should stay LOW");
    }
}

#else
// Для WB74 - проверяем что функции возвращают заглушки

// Сценарий: вызов wbmz_is_charging_enabled() на WB74 (без GPIO управления зарядом)
// Ожидается: возвращает true (заряд всегда включен схемотехникой)
static void test_charging_always_enabled_without_control(void)
{
    LOG_INFO("Testing charging always enabled when no control GPIO (WB74)");

    wbmz_init();

    bool enabled = wbmz_is_charging_enabled();
    TEST_ASSERT_TRUE_MESSAGE(enabled, "Charging should always return true when no control GPIO");
}


// Сценарий: вызов wbmz_is_vbat_ok() на WB74 (без мониторинга VBAT)
// Ожидается: возвращает true (считаем что напряжение в норме)
static void test_vbat_always_ok_without_monitoring(void)
{
    LOG_INFO("Testing VBAT always OK when no monitoring (WB74)");

    wbmz_init();

    bool vbat_ok = wbmz_is_vbat_ok();
    TEST_ASSERT_TRUE_MESSAGE(vbat_ok, "VBAT should always return true when no monitoring");
}


// Сценарий: вызов wbmz_set_charging_force_control() на WB74 (заглушка)
// Ожидается: функция ничего не делает, не падает
static void test_charging_force_control_stub(void)
{
    LOG_INFO("Testing charging force control stub (WB74)");

    wbmz_init();

    wbmz_set_charging_force_control(true, true);
    wbmz_set_charging_force_control(false, false);

    bool enabled = wbmz_is_charging_enabled();
    TEST_ASSERT_TRUE_MESSAGE(enabled, "Charging should always be enabled (stub behavior)");
}

#endif


// ============================================================================
// Main test runner
// ============================================================================

int main(void)
{
    UNITY_BEGIN();

    // Тесты инициализации
    RUN_TEST(test_wbmz_init);

    // Тесты powered_from_wbmz
    RUN_TEST(test_powered_from_wbmz_when_gpio_low);
    RUN_TEST(test_not_powered_from_wbmz_when_gpio_high);

    // Тесты прямого управления stepup
    RUN_TEST(test_stepup_enable_disable);

    // Тесты автоматического управления stepup
    RUN_TEST(test_stepup_auto_enable_when_vin_ok_and_no_usb);
    RUN_TEST(test_stepup_not_enabled_when_usb_present);
    #if !defined(EC_USB_HUB_DEBUG_NETWORK)
        RUN_TEST(test_stepup_not_enabled_when_usb_network_present);
    #endif
    RUN_TEST(test_stepup_not_enabled_when_vin_not_ok);
    RUN_TEST(test_stepup_auto_disable_when_battery_discharged);
    RUN_TEST(test_stepup_discharge_filter_reset);
    RUN_TEST(test_stepup_stays_enabled_when_powered_from_wbmz);
    RUN_TEST(test_stepup_stays_enabled_when_usb_connected);

    // Тесты force control для stepup
    RUN_TEST(test_stepup_force_control_enable);
    RUN_TEST(test_stepup_force_control_disable);
    RUN_TEST(test_stepup_force_control_release);
    RUN_TEST(test_stepup_force_control_switch_on_to_off);

    // Тесты управления зарядом
    #if defined WBEC_GPIO_WBMZ_CHARGE_ENABLE
        RUN_TEST(test_charging_auto_enable_when_vin_ok_and_not_powered_from_wbmz);
        RUN_TEST(test_charging_auto_disable_when_vin_not_ok);
        RUN_TEST(test_charging_auto_disable_when_powered_from_wbmz);
        RUN_TEST(test_charging_stays_disabled_when_vin_not_ok);
        RUN_TEST(test_charging_force_control_enable);
        RUN_TEST(test_charging_force_control_disable);
        RUN_TEST(test_charging_force_control_disable_when_enabled);
        RUN_TEST(test_charging_force_control_release);
        RUN_TEST(test_vbat_ok_status);

        // Тесты стабильности force control
        RUN_TEST(test_stepup_force_control_stability_enabled);
        RUN_TEST(test_stepup_force_control_stability_disabled);
        RUN_TEST(test_charging_force_control_stability_enabled);
        RUN_TEST(test_charging_force_control_stability_disabled);
    #else
        RUN_TEST(test_charging_always_enabled_without_control);
        RUN_TEST(test_vbat_always_ok_without_monitoring);
        RUN_TEST(test_charging_force_control_stub);
    #endif

    return UNITY_END();
}
