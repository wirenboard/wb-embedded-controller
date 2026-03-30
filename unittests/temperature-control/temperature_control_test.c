#include "unity.h"
#include "temperature-control.h"
#include "utest_ntc.h"
#include "utest_wbmz_common.h"
#include "utest_voltage_monitor.h"
#include "voltage-monitor.h"
#include "utest_gpio.h"
#include "config.h"

#define LOG_LEVEL LOG_LEVEL_INFO
#include "console_log.h"

#if defined(EC_GPIO_HEATER)
static const gpio_pin_t heater_pin = { EC_GPIO_HEATER };
#endif


void setUp(void)
{
    // Сброс состояния GPIO
    utest_gpio_reset_instances();

    // Установка значений по умолчанию
    utest_ntc_set_temperature(F16(25.0));  // 25°C
    utest_wbmz_set_powered_from_wbmz(false);
    vmon_init();
}

void tearDown(void)
{

}

// Сценарий: Инициализация подсистемы управления температурой
// Ожидается: Инициализация завершается успешно, без ошибок
static void test_temperature_control_init(void)
{
    LOG_INFO("Testing initialization");

    temperature_control_init();
    // Инициализация должна пройти без ошибок
    TEST_PASS();
}

#if defined(EC_GPIO_HEATER)
// Сценарий: Инициализация управления температурой с поддержкой нагревателя
// Ожидается: GPIO нагревателя настроен как push-pull выход, начальное состояние LOW (выключен)
static void test_heater_gpio_init(void)
{
    LOG_INFO("Testing heater GPIO initialization");

    temperature_control_init();

    // Проверка, что GPIO нагревателя настроен как выход
    uint32_t mode = utest_gpio_get_mode(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(GPIO_MODE_OUTPUT, mode, "Heater GPIO should be configured as OUTPUT");

    // Проверка, что GPIO нагревателя настроен как push-pull
    uint32_t otype = utest_gpio_get_output_type(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(GPIO_OTYPE_PUSH_PULL, otype, "Heater GPIO should be configured as PUSH-PULL");

    // Проверка, что после инициализации нагреватель выключен
    uint32_t state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Heater GPIO should be LOW (off) after initialization");
}
#endif

// Сценарий: Проверка готовности при температуре выше минимальной рабочей
// Ожидается: temperature_control_is_temperature_ready() возвращает true
static void test_temperature_is_ready_above_minimum(void)
{
    LOG_INFO("Testing temperature ready when above minimum");

    // Установка температуры выше минимума (WBEC_MINIMUM_WORKING_TEMPERATURE)
    utest_ntc_set_temperature(F16(WBEC_MINIMUM_WORKING_TEMPERATURE + 1.0));

    bool ready = temperature_control_is_temperature_ready();
    TEST_ASSERT_TRUE_MESSAGE(ready, "Temperature should be ready when above minimum working temperature");
}

// Сценарий: Проверка готовности при температуре ниже минимальной рабочей
// Ожидается: temperature_control_is_temperature_ready() возвращает false
static void test_temperature_is_not_ready_below_minimum(void)
{
    LOG_INFO("Testing temperature not ready when below minimum");

    // Установка температуры ниже минимума (WBEC_MINIMUM_WORKING_TEMPERATURE)
    utest_ntc_set_temperature(F16(WBEC_MINIMUM_WORKING_TEMPERATURE - 10.0));

    bool ready = temperature_control_is_temperature_ready();
    TEST_ASSERT_FALSE_MESSAGE(ready, "Temperature should not be ready when below minimum working temperature");
}

// Сценарий: Проверка готовности при температуре на точном минимальном пороге
// Ожидается: false на пороге, true при превышении на 1°C
static void test_temperature_is_ready_at_minimum_threshold(void)
{
    LOG_INFO("Testing temperature ready at threshold");

    // Установка температуры ровно на минимальном пороге
    utest_ntc_set_temperature(F16(WBEC_MINIMUM_WORKING_TEMPERATURE));

    bool ready = temperature_control_is_temperature_ready();
    TEST_ASSERT_FALSE_MESSAGE(ready, "Temperature should not be ready at exact minimum threshold");

    // Чуть выше порога
    utest_ntc_set_temperature(F16(WBEC_MINIMUM_WORKING_TEMPERATURE + 1.0));

    ready = temperature_control_is_temperature_ready();
    TEST_ASSERT_TRUE_MESSAGE(ready, "Temperature should be ready above minimum threshold");
}

// Сценарий: Получение температуры в единицах 0.01°C для положительной температуры
// Ожидается: Возвращается температура, умноженная на 100 (например, 25.5°C -> 2550)
static void test_get_temperature_positive(void)
{
    LOG_INFO("Testing get temperature - positive values");

    utest_ntc_set_temperature(F16(25.5));

    int16_t temp_x100 = temperature_control_get_temperature_c_x100();
    TEST_ASSERT_EQUAL_INT16_MESSAGE(2550, temp_x100, "Temperature x100 should be 2550 for 25.5°C");
}

// Сценарий: Получение температуры в единицах 0.01°C для отрицательной температуры
// Ожидается: Возвращается температура, умноженная на 100 (например, -15.5°C -> -1550)
static void test_get_temperature_negative(void)
{
    LOG_INFO("Testing get temperature - negative values");

    utest_ntc_set_temperature(F16(-15.5));

    int16_t temp_x100 = temperature_control_get_temperature_c_x100();
    TEST_ASSERT_EQUAL_INT16_MESSAGE(-1550, temp_x100, "Temperature x100 should be -1550 for -15.5°C");
}

// Сценарий: Получение температуры ровно при 0°C
// Ожидается: Возвращается 0
static void test_get_temperature_zero(void)
{
    LOG_INFO("Testing get temperature - zero");

    utest_ntc_set_temperature(F16(0.0));

    int16_t temp_x100 = temperature_control_get_temperature_c_x100();
    TEST_ASSERT_EQUAL_INT16_MESSAGE(0, temp_x100, "Temperature x100 should be 0 for 0.0°C");
}


#if defined(EC_GPIO_HEATER)
// Сценарий: Температура ниже порога включения нагревателя при наличии питания Vin
// Ожидается: GPIO нагревателя переходит в HIGH (нагреватель включен)
static void test_heater_turns_on_at_low_temperature(void)
{
    LOG_INFO("Testing heater turns on at low temperature");

    temperature_control_init();

    // Установка температуры ниже порога включения нагревателя (EC_HEATER_ON_TEMP)
    utest_ntc_set_temperature(F16(EC_HEATER_ON_TEMP - 1.0));

    // Установка питания от Vin (для нагревателя требуется наличие Vin)
    utest_wbmz_set_powered_from_wbmz(false);
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);

    temperature_control_do_periodic_work();

    // Проверка, что GPIO нагревателя в HIGH (вкл)
    uint32_t state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Heater GPIO should be HIGH (on) at low temperature");
}

// Сценарий: Температура поднимается выше порога выключения при включенном нагревателе
// Ожидается: GPIO нагревателя переходит в LOW (нагреватель выключен)
static void test_heater_turns_off_at_high_temperature(void)
{
    LOG_INFO("Testing heater turns off at high temperature");

    temperature_control_init();

    // Сначала включаем нагреватель
    utest_ntc_set_temperature(F16(EC_HEATER_ON_TEMP - 1.0));
    utest_wbmz_set_powered_from_wbmz(false);
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);
    temperature_control_do_periodic_work();

    // Убеждаемся, что нагреватель включен
    uint32_t state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Heater should be on initially");

    // Затем устанавливаем температуру выше порога выключения (EC_HEATER_OFF_TEMP)
    utest_ntc_set_temperature(F16(EC_HEATER_OFF_TEMP + 1.0));
    temperature_control_do_periodic_work();

    // Проверка, что GPIO нагревателя в LOW (выключен)
    state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Heater GPIO should be LOW (off) at high temperature");
}

// Сценарий: Низкая температура, но система питается от WBMZ (без Vin)
// Ожидается: Нагреватель остается в LOW (выключен), чтобы не потреблять питание от WBMZ
static void test_heater_stays_off_when_powered_from_wbmz(void)
{
    LOG_INFO("Testing heater stays off when powered from WBMZ");

    temperature_control_init();

    // Установка низкой температуры
    utest_ntc_set_temperature(F16(EC_HEATER_ON_TEMP - 1.0));

    // Установка питания от WBMZ (нагреватель не должен включиться)
    utest_wbmz_set_powered_from_wbmz(true);
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, false);

    temperature_control_do_periodic_work();

    // Проверка, что GPIO нагревателя в LOW (выключен)
    uint32_t state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Heater GPIO should stay LOW (off) when powered from WBMZ");
}

// Сценарий: Низкая температура, но Vin отсутствует
// Ожидается: Нагреватель остается в LOW (выключен) из-за отсутствия входного питания
static void test_heater_stays_off_when_vin_not_present(void)
{
    LOG_INFO("Testing heater stays off when Vin not present");

    temperature_control_init();

    // Установка низкой температуры
    utest_ntc_set_temperature(F16(EC_HEATER_ON_TEMP - 1.0));

    // Отключим Vin (нагреватель не должен включиться)
    utest_wbmz_set_powered_from_wbmz(false);
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, false);

    temperature_control_do_periodic_work();

    // Проверка, что GPIO нагревателя в LOW (выключен)
    uint32_t state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Heater GPIO should stay LOW (off) when Vin not present");
}

// Сценарий: Циклическое изменение температуры рядом с порогами ON/OFF
// Ожидается: Гистерезис предотвращает частые переключения; нагреватель включается ниже
// порога ON и выключается выше порога OFF
static void test_heater_hysteresis(void)
{
    LOG_INFO("Testing heater hysteresis");

    temperature_control_init();

    // Порог включения нагревателя: EC_HEATER_ON_TEMP, порог выключения: EC_HEATER_OFF_TEMP
    utest_wbmz_set_powered_from_wbmz(false);
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);

    // Начинаем с низкой температуры: нагреватель должен включиться
    utest_ntc_set_temperature(F16(EC_HEATER_ON_TEMP - 1.0));
    temperature_control_do_periodic_work();
    uint32_t state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Heater should turn ON below ON threshold");

    // Температура растет, но все еще ниже порога OFF
    utest_ntc_set_temperature(F16((EC_HEATER_ON_TEMP + EC_HEATER_OFF_TEMP) / 2.0));
    temperature_control_do_periodic_work();
    // Нагреватель должен оставаться включенным (выше ON, ниже OFF)
    state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Heater should stay ON inside hysteresis band");

    // Температура поднимается выше порога OFF
    utest_ntc_set_temperature(F16(EC_HEATER_OFF_TEMP + 1.0));
    temperature_control_do_periodic_work();
    // Нагреватель должен выключиться
    state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Heater should turn OFF above OFF threshold");

    // Температура падает, но все еще выше порога ON
    utest_ntc_set_temperature(F16((EC_HEATER_ON_TEMP + EC_HEATER_OFF_TEMP) / 2.0));
    temperature_control_do_periodic_work();
    // Нагреватель должен оставаться выключенным (ниже OFF, выше ON)
    state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Heater should stay OFF inside hysteresis band");

    // Температура падает ниже порога ON
    utest_ntc_set_temperature(F16(EC_HEATER_ON_TEMP - 1.0));
    temperature_control_do_periodic_work();
    // Нагреватель должен снова включиться
    state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Heater should turn ON again below ON threshold");
}

// Сценарий: Нагреватель работает от Vin, затем система переключается на питание WBMZ
// Ожидается: Нагреватель автоматически выключается при смене источника питания
static void test_heater_turns_off_when_wbmz_power_enabled_during_heating(void)
{
    LOG_INFO("Testing heater turns off when switched to WBMZ power during heating");

    temperature_control_init();

    // Установка низкой температуры и корректных условий питания для включения нагревателя
    utest_ntc_set_temperature(F16(EC_HEATER_ON_TEMP - 1.0));
    utest_wbmz_set_powered_from_wbmz(false);
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);

    temperature_control_do_periodic_work();

    // Убеждаемся, что нагреватель включился
    uint32_t state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Heater should be ON at low temperature with Vin");

    // Теперь переключаемся на питание WBMZ при работающем нагревателе
    utest_wbmz_set_powered_from_wbmz(true);
    temperature_control_do_periodic_work();

    // Нагреватель должен автоматически выключиться
    state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Heater should turn OFF when switched to WBMZ power");
}

// Сценарий: Нагреватель работает от Vin, затем Vin пропадает
// Ожидается: Нагреватель автоматически выключается при пропадании Vin
static void test_heater_turns_off_when_vin_lost_during_heating(void)
{
    LOG_INFO("Testing heater turns off when Vin is lost during heating");

    temperature_control_init();

    // Установка низкой температуры и корректных условий питания для включения нагревателя
    utest_ntc_set_temperature(F16(EC_HEATER_ON_TEMP - 1.0));
    utest_wbmz_set_powered_from_wbmz(false);
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);

    temperature_control_do_periodic_work();

    // Убеждаемся, что нагреватель включился
    uint32_t state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Heater should be ON at low temperature with Vin");

    // Теперь отключить Vin при работающем нагревателе
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, false);
    temperature_control_do_periodic_work();

    // Нагреватель должен автоматически выключиться
    state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Heater should turn OFF when Vin is lost");
}

// Сценарий: Принудительное включение нагревателя при высокой температуре (в обход штатного управления)
// Ожидается: Нагреватель включается, несмотря на температуру выше порога
static void test_heater_force_control_enable(void)
{
    LOG_INFO("Testing heater force control enable");

    temperature_control_init();

    // Установка высокой температуры (обычно нагреватель должен быть выключен при этом)
    utest_ntc_set_temperature(F16(25.0));
    utest_wbmz_set_powered_from_wbmz(false);
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);

    // Принудительно включаем нагреватель
    temperature_control_heater_force_control(true);
    uint32_t state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Heater should turn ON immediately in force mode");

    // Нагреватель должен быть включен, несмотря на высокую температуру
    temperature_control_do_periodic_work();
    state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Heater should stay ON in force mode");
}

// Сценарий: Отключение принудительного режима нагревателя, затем установка низкой температуры
// Ожидается: Нагреватель возвращается к обычному управлению по температуре
static void test_heater_force_control_disable(void)
{
    LOG_INFO("Testing heater force control disable");

    temperature_control_init();

    // Сначала принудительно включаем нагреватель
    utest_ntc_set_temperature(F16(25.0));
    utest_wbmz_set_powered_from_wbmz(false);
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);
    temperature_control_heater_force_control(true);
    uint32_t state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Heater should be ON in force mode");

    // Отключаем принудительное управление
    temperature_control_heater_force_control(false);
    temperature_control_do_periodic_work();
    state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "Heater should turn OFF after force mode is disabled at high temperature");

    // Установка низкой температуры
    utest_ntc_set_temperature(F16(EC_HEATER_ON_TEMP - 1.0));
    temperature_control_do_periodic_work();
    // Теперь нагреватель должен управляться температурой
    state = utest_gpio_get_output_state(heater_pin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "Heater should be controlled by temperature after force mode is disabled");
}
#endif


int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_temperature_control_init);
    RUN_TEST(test_temperature_is_ready_above_minimum);
    RUN_TEST(test_temperature_is_not_ready_below_minimum);
    RUN_TEST(test_temperature_is_ready_at_minimum_threshold);
    RUN_TEST(test_get_temperature_positive);
    RUN_TEST(test_get_temperature_negative);
    RUN_TEST(test_get_temperature_zero);

#if defined(EC_GPIO_HEATER)
    RUN_TEST(test_heater_gpio_init);
    RUN_TEST(test_heater_turns_on_at_low_temperature);
    RUN_TEST(test_heater_turns_off_at_high_temperature);
    RUN_TEST(test_heater_stays_off_when_powered_from_wbmz);
    RUN_TEST(test_heater_stays_off_when_vin_not_present);
    RUN_TEST(test_heater_turns_off_when_wbmz_power_enabled_during_heating);
    RUN_TEST(test_heater_turns_off_when_vin_lost_during_heating);
    RUN_TEST(test_heater_hysteresis);
    RUN_TEST(test_heater_force_control_enable);
    RUN_TEST(test_heater_force_control_disable);
#endif

    return UNITY_END();
}
