#include "unity.h"
#include "gpio-subsystem.h"
#include "config.h"
#include "utest_gpio.h"
#include "regmap-int.h"
#include "regmap-structs.h"
#include "utest_regmap.h"
#include "voltage-monitor.h"
#include "utest_voltage_monitor.h"
#include "bits.h"

#ifdef EC_MOD1_MOD2_GPIO_CONTROL
#include "shared-gpio.h"
#include "utest_shared_gpio.h"
#endif

#define LOG_LEVEL LOG_LEVEL_INFO
#include "console_log.h"

// Перечисление GPIO, соответствующее gpio-subsytem.c
enum ec_ext_gpio {
    EC_EXT_GPIO_A1,
    EC_EXT_GPIO_A2,
    EC_EXT_GPIO_A3,
    EC_EXT_GPIO_A4,
    EC_EXT_GPIO_V_OUT,
    // Порядок TX, RX, RTS должен соответствовать порядку в enum mod_gpio из shared-gpio.h
    EC_EXT_GPIO_MOD1_TX,
    EC_EXT_GPIO_MOD1_RX,
    EC_EXT_GPIO_MOD1_RTS,
    EC_EXT_GPIO_MOD2_TX,
    EC_EXT_GPIO_MOD2_RX,
    EC_EXT_GPIO_MOD2_RTS,

    EC_EXT_GPIO_COUNT
};

// Значения в регионе GPIO_AF (2 бита на пин), соответствующие gpio-subsytem.c
enum gpio_regmap_af {
    GPIO_REGMAP_AF_GPIO = 0,
    GPIO_REGMAP_AF_UART = 1,
};

#ifdef EC_MOD1_MOD2_GPIO_CONTROL
// Индекс AF для MOD GPIO (вычисляется как: mod * MOD_GPIO_COUNT + mod_gpio)
enum gpio_af_index {
    GPIO_AF_MOD1_TX = 0,
    GPIO_AF_MOD1_RX = 1,
    GPIO_AF_MOD1_RTS = 2,
    GPIO_AF_MOD2_TX = 3,
    GPIO_AF_MOD2_RX = 4,
    GPIO_AF_MOD2_RTS = 5,
};

// Вычисление индекса AF из mod и mod_gpio (соответствует логике gpio-subsytem.c)
#define GPIO_AF_INDEX(mod, mod_gpio) ((mod) * MOD_GPIO_COUNT + (mod_gpio))

// Вспомогательный макрос для установки значения AF для конкретного MOD GPIO
#define GPIO_AF_SET(gpio_af_index, af_value) ((af_value) << ((gpio_af_index) * 2))
#endif

// Пин V_OUT GPIO
static const gpio_pin_t v_out_gpio = { EC_GPIO_VOUT_EN };

void setUp(void)
{
    // Сброс всех состояний моков
    utest_gpio_reset_instances();
    utest_regmap_reset();
    utest_vmon_reset();

#ifdef EC_MOD1_MOD2_GPIO_CONTROL
    utest_shared_gpio_reset();
#endif
}

void tearDown(void)
{

}

// Сценарий: Инициализация подсистемы GPIO
// Ожидается: пин V_OUT сконфигурирован как выход push-pull и установлен в LOW,
// все разделяемые GPIO в режиме INPUT
static void test_gpio_init(void)
{
    LOG_INFO("Testing GPIO initialization");

    gpio_init();

    // Проверка, что V_OUT GPIO сконфигурирован как выход
    uint32_t mode = utest_gpio_get_mode(v_out_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(GPIO_MODE_OUTPUT, mode, "V_OUT GPIO should be configured as OUTPUT");

    // Проверка, что V_OUT GPIO сконфигурирован как push-pull
    uint32_t otype = utest_gpio_get_output_type(v_out_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(GPIO_OTYPE_PUSH_PULL, otype, "V_OUT GPIO should be configured as PUSH-PULL");

    // Проверка, что V_OUT выключен после инициализации
    uint32_t state = utest_gpio_get_output_state(v_out_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "V_OUT GPIO should be LOW (off) after initialization");

#ifdef EC_MOD1_MOD2_GPIO_CONTROL
    // Проверка, что shared_gpio был инициализирован (все GPIO должны быть в режиме INPUT)
    for (unsigned mod = 0; mod < MOD_COUNT; mod++) {
        for (unsigned gpio = 0; gpio < MOD_GPIO_COUNT; gpio++) {
            enum mod_gpio_mode gpio_mode = utest_shared_gpio_get_mode(mod, gpio);
            TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_INPUT, gpio_mode, "All shared GPIOs should be in INPUT mode after initialization");
        }
    }
#endif
}

// Сценарий: Сброс подсистемы GPIO после инициализации
// Ожидается: Все регионы regmap инициализированы, MOD GPIO установлены в режим INPUT
static void test_gpio_reset(void)
{
    LOG_INFO("Testing GPIO reset");

    gpio_init();
    gpio_reset();

    // После сброса проверка, что регионы regmap инициализированы
    struct REGMAP_GPIO_CTRL gpio_ctrl;
    bool result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_CTRL region after reset");

    struct REGMAP_GPIO_DIR gpio_dir;
    result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_DIR region after reset");

    struct REGMAP_GPIO_AF gpio_af;
    result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_AF, &gpio_af, sizeof(gpio_af));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_AF region after reset");

#ifdef EC_MOD1_MOD2_GPIO_CONTROL
    // После сброса все MOD GPIO должны быть в режиме INPUT
    for (unsigned mod = 0; mod < MOD_COUNT; mod++) {
        for (unsigned gpio = 0; gpio < MOD_GPIO_COUNT; gpio++) {
            enum mod_gpio_mode mode = utest_shared_gpio_get_mode(mod, gpio);
            TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_INPUT, mode, "All MOD GPIOs should be in INPUT mode after reset");
        }
    }
#endif
}


#ifdef EC_MOD1_MOD2_GPIO_CONTROL
// Сценарий: Изменение направления MOD GPIO с INPUT на OUTPUT через regmap
// Ожидается: Режим GPIO изменяется на OUTPUT в shared GPIO
static void test_gpio_direction_change_input_to_output(void)
{
    LOG_INFO("Testing GPIO direction change from INPUT to OUTPUT");

    gpio_init();
    gpio_reset();

    // Устанавливаем MOD2_RX как выход
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD2_RX);
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);

    gpio_do_periodic_work();

    // Проверка, что режим MOD2_RX изменился на OUTPUT
    enum mod_gpio_mode mode = utest_shared_gpio_get_mode(MOD2, MOD_GPIO_RX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_OUTPUT, mode, "MOD2_RX should be in OUTPUT mode");
}

// Сценарий: Изменение направления MOD GPIO с OUTPUT обратно на INPUT через regmap
// Ожидается: Режим GPIO изменяется обратно на INPUT в shared GPIO
static void test_gpio_direction_change_output_to_input(void)
{
    LOG_INFO("Testing GPIO direction change from OUTPUT to INPUT");

    gpio_init();
    gpio_reset();

    // Сначала устанавливаем MOD1_RTS как выход
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD1_RTS);
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Теперь изменяем обратно на вход
    gpio_dir.gpio_dir = 0;
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Проверка, что режим MOD1_RTS изменился обратно на INPUT
    enum mod_gpio_mode mode = utest_shared_gpio_get_mode(MOD1, MOD_GPIO_RTS);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_INPUT, mode, "MOD1_RTS should be in INPUT mode");
}

// Сценарий: Установка MOD GPIO как выход и изменение его значения через regmap
// Ожидается: Состояние на выходе правильно изменяется с HIGH на LOW
static void test_gpio_set_output_value(void)
{
    LOG_INFO("Testing GPIO output value setting");

    gpio_init();
    gpio_reset();

    // Устанавливаем MOD2_TX как выход
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD2_TX);
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Устанавливаем состояние на выходе в HIGH
    struct REGMAP_GPIO_CTRL gpio_ctrl;
    gpio_ctrl.gpio_ctrl = BIT(EC_EXT_GPIO_MOD2_TX);  // MOD2_TX в HIGH
    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);
    gpio_do_periodic_work();

    // Проверка состояния на выходе
    bool value = utest_shared_gpio_get_output_value(MOD2, MOD_GPIO_TX);
    TEST_ASSERT_TRUE_MESSAGE(value, "MOD2_TX output should be HIGH");

    // Устанавливаем состояния на выходе в LOW
    gpio_ctrl.gpio_ctrl = 0;
    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);
    gpio_do_periodic_work();

    // Проверка состояния на выходе
    value = utest_shared_gpio_get_output_value(MOD2, MOD_GPIO_TX);
    TEST_ASSERT_FALSE_MESSAGE(value, "MOD2_TX output should be LOW");
}

// Сценарий: Чтение состояния входа MOD GPIO и отражение его в regmap
// Ожидается: состояние входа (HIGH/LOW) правильно отображается в регистре GPIO_CTRL
static void test_gpio_read_input_value(void)
{
    LOG_INFO("Testing GPIO input value reading");

    gpio_init();
    gpio_reset();

    // MOD1_RX находится в режиме INPUT по умолчанию после сброса
    // Установка состояния входа в HIGH
    utest_shared_gpio_set_input_value(MOD1, MOD_GPIO_RX, true);

    gpio_do_periodic_work();

    // Читаем обратно GPIO_CTRL для проверки состояния входа
    struct REGMAP_GPIO_CTRL gpio_ctrl;
    bool result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_CTRL");
    TEST_ASSERT_TRUE_MESSAGE(gpio_ctrl.gpio_ctrl & BIT(EC_EXT_GPIO_MOD1_RX), "MOD1_RX input should be HIGH");

    // Установка состояния входа в LOW
    utest_shared_gpio_set_input_value(MOD1, MOD_GPIO_RX, false);
    gpio_do_periodic_work();

    result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_CTRL");
    TEST_ASSERT_FALSE_MESSAGE(gpio_ctrl.gpio_ctrl & BIT(EC_EXT_GPIO_MOD1_RX), "MOD1_RX input should be LOW");
}

// Сценарий: Установка MOD GPIO в альтернативную функцию UART через regmap
// Ожидается: режим GPIO изменяется на AF_UART в shared GPIO
static void test_gpio_af_mode_uart(void)
{
    LOG_INFO("Testing GPIO AF mode - UART");

    gpio_init();
    gpio_reset();

    // Установка MOD1_TX в режим UART
    struct REGMAP_GPIO_AF gpio_af;
    gpio_af.af = GPIO_AF_SET(GPIO_AF_MOD1_TX, GPIO_REGMAP_AF_UART);
    regmap_set_region_data(REGMAP_REGION_GPIO_AF, &gpio_af, sizeof(gpio_af));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_AF);
    gpio_do_periodic_work();

    // Проверка, что MOD1_TX в режиме UART
    enum mod_gpio_mode mode = utest_shared_gpio_get_mode(MOD1, MOD_GPIO_TX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_AF_UART, mode, "MOD1_TX should be in UART AF mode");
}

// Сценарий: Попытка изменить направление GPIO, находящегося в режиме AF UART
// Ожидается: GPIO остается в режиме AF_UART, изменение направления игнорируется
static void test_gpio_af_mode_prevents_direction_change(void)
{
    LOG_INFO("Testing that AF mode prevents direction change");

    gpio_init();
    gpio_reset();

    // Установка MOD2_RX в режим UART
    struct REGMAP_GPIO_AF gpio_af;
    gpio_af.af = GPIO_AF_SET(GPIO_AF_MOD2_RX, GPIO_REGMAP_AF_UART);
    regmap_set_region_data(REGMAP_REGION_GPIO_AF, &gpio_af, sizeof(gpio_af));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_AF);
    gpio_do_periodic_work();

    // Попытка изменить направление на OUTPUT
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD2_RX);
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Режим все еще должен быть UART, а не OUTPUT
    enum mod_gpio_mode mode = utest_shared_gpio_get_mode(MOD2, MOD_GPIO_RX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_AF_UART, mode, "MOD2_RX should remain in UART AF mode");
}

// Сценарий: Установка состояния выхода GPIO в regmap, затем изменение направления пина на выход
// Ожидается: состояние выхода применяется, когда пин становится выходом
static void test_gpio_output_value_preserved_on_direction_change(void)
{
    LOG_INFO("Testing that output value is preserved when changing direction");

    gpio_init();
    gpio_reset();

    // Шаг 1: Установка желаемого состояния выхода, пока пин еще в режиме входа
    struct REGMAP_GPIO_CTRL gpio_ctrl;
    gpio_ctrl.gpio_ctrl = BIT(EC_EXT_GPIO_MOD2_RTS);  // MOD2_RTS в HIGH
    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);
    gpio_do_periodic_work();

    // Шаг 2: Изменение направление на выход
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD2_RTS);  // MOD2_RTS в OUTPUT
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Проверка, что состояние выхода было установлено в HIGH, как запрошено
    bool value = utest_shared_gpio_get_output_value(MOD2, MOD_GPIO_RTS);
    TEST_ASSERT_TRUE_MESSAGE(value, "MOD2_RTS output should be HIGH after direction change");
}

// Сценарий: Изменение GPIO с входа на выход без установки gpio_ctrl
// Ожидается: текущее состояние входа сохраняется как начальное состояние выхода
static void test_gpio_output_value_preserved_without_request(void)
{
    LOG_INFO("Testing that output value is preserved without prior request");

    gpio_init();
    gpio_reset();

    // Установка MOD1_TX как вход с состоянием HIGH
    utest_shared_gpio_set_input_value(MOD1, MOD_GPIO_TX, true);
    gpio_do_periodic_work();

    // Теперь изменяем на выход БЕЗ предварительной установки gpio_ctrl
    // Текущее состояние входа должно быть сохранено
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD1_TX);
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Проверка, что состояние выхода соответствует предыдущему состоянию входа (HIGH)
    bool value = utest_shared_gpio_get_output_value(MOD1, MOD_GPIO_TX);
    TEST_ASSERT_TRUE_MESSAGE(value, "MOD1_TX output should preserve input HIGH value");
}

// Сценарий: Установка GPIO в режим AF UART и попытка изменить направление
// Ожидается: бит gpio_ctrl очищается для пина в режиме AF
static void test_gpio_af_mode_clears_ctrl_bit(void)
{
    LOG_INFO("Testing that AF mode clears gpio_ctrl bit when direction changes");

    gpio_init();
    gpio_reset();

    // Установка MOD1_RX в HIGH в режиме входа
    utest_shared_gpio_set_input_value(MOD1, MOD_GPIO_RX, true);
    gpio_do_periodic_work();

    // Проверка, что gpio_ctrl имеет установленный бит
    struct REGMAP_GPIO_CTRL gpio_ctrl;
    bool result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_CTRL");
    TEST_ASSERT_TRUE_MESSAGE(gpio_ctrl.gpio_ctrl & BIT(EC_EXT_GPIO_MOD1_RX), "MOD1_RX bit should be set initially");

    // Установка MOD1_RX в режим UART
    struct REGMAP_GPIO_AF gpio_af;
    gpio_af.af = GPIO_AF_SET(GPIO_AF_MOD1_RX, GPIO_REGMAP_AF_UART);
    regmap_set_region_data(REGMAP_REGION_GPIO_AF, &gpio_af, sizeof(gpio_af));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_AF);
    gpio_do_periodic_work();

    // Попытка изменить направление
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD1_RX);
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Бит gpio_ctrl должен быть очищен для пина в режиме AF
    result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_CTRL");
    TEST_ASSERT_FALSE_MESSAGE(gpio_ctrl.gpio_ctrl & BIT(EC_EXT_GPIO_MOD1_RX), "MOD1_RX bit should be cleared in AF mode");
}

// Сценарий: Попытка изменить направление пина V_OUT на вход через regmap
// Ожидается: V_OUT всегда остается выходом в regmap
static void test_gpio_dir_preserves_v_out_as_output(void)
{
    LOG_INFO("Testing that V_OUT always remains as output");

    gpio_init();
    gpio_reset();

    // Попытка установить все GPIO как входы (включая V_OUT)
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = 0;
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Читаем обратно GPIO_DIR - бит V_OUT все еще должен быть установлен
    bool result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_DIR");
    TEST_ASSERT_TRUE_MESSAGE(gpio_dir.gpio_dir & BIT(EC_EXT_GPIO_V_OUT), "V_OUT should always be output");
}

// Сценарий: Установка GPIO в режим GPIO (не AF) и переключение направления
// Ожидается: режим GPIO переключается между OUTPUT и INPUT
static void test_gpio_af_switches_mode_based_on_direction(void)
{
    LOG_INFO("Testing AF mode switches between INPUT/OUTPUT based on direction");

    gpio_init();
    gpio_reset();

    // Установка MOD2_TX в режим GPIO (AF = 0) и как OUTPUT
    struct REGMAP_GPIO_AF gpio_af;
    gpio_af.af = GPIO_AF_SET(GPIO_AF_MOD2_TX, GPIO_REGMAP_AF_GPIO);
    regmap_set_region_data(REGMAP_REGION_GPIO_AF, &gpio_af, sizeof(gpio_af));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_AF);

    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD2_TX);
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Должен быть OUTPUT
    enum mod_gpio_mode mode = utest_shared_gpio_get_mode(MOD2, MOD_GPIO_TX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_OUTPUT, mode, "MOD2_TX should be OUTPUT");

    // Смена направление на INPUT (сохраняя AF = GPIO)
    gpio_dir.gpio_dir = 0;
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Теперь должен быть INPUT
    mode = utest_shared_gpio_get_mode(MOD2, MOD_GPIO_TX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_INPUT, mode, "MOD2_TX should be INPUT");
}

// СЦенарий: Изменение состояния выхода одного GPIO оставляет другой без изменений
// Ожидается: состояние изменяется только у измененного GPIO, остальные остаются как было установлено
static void test_gpio_values_only_set_for_changed_pins(void)
{
    LOG_INFO("Testing that GPIO values are only set for changed pins");

    gpio_init();
    gpio_reset();

    // Установка MOD1_TX и MOD2_RX на выход
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD1_TX) | BIT(EC_EXT_GPIO_MOD2_RX);
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Установка в LOW изначально
    struct REGMAP_GPIO_CTRL gpio_ctrl;
    gpio_ctrl.gpio_ctrl = 0;
    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);
    gpio_do_periodic_work();

    // Изменения состояния только MOD1_TX на HIGH (MOD2_RX остается LOW)
    gpio_ctrl.gpio_ctrl = BIT(EC_EXT_GPIO_MOD1_TX);
    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);
    gpio_do_periodic_work();

    // MOD1_TX должен быть HIGH
    bool value = utest_shared_gpio_get_output_value(MOD1, MOD_GPIO_TX);
    TEST_ASSERT_TRUE_MESSAGE(value, "MOD1_TX should be HIGH");

    // MOD2_RX все еще должен быть LOW (без изменений)
    value = utest_shared_gpio_get_output_value(MOD2, MOD_GPIO_RX);
    TEST_ASSERT_FALSE_MESSAGE(value, "MOD2_RX should be LOW");
}

// Сценарий: Чтение состояний GPIO, когда некоторые пины - выходы, а некоторые - входы
// Ожидается: читаются только INPUT пины, OUTPUT пины сообщают свое установленное значение
static void test_gpio_collect_only_reads_inputs(void)
{
    LOG_INFO("Testing that collect_gpio_states only reads INPUT pins");

    gpio_init();
    gpio_reset();

    // Установка MOD1_TX как OUTPUT со значением HIGH
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD1_TX);
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);

    struct REGMAP_GPIO_CTRL gpio_ctrl;
    gpio_ctrl.gpio_ctrl = BIT(EC_EXT_GPIO_MOD1_TX);
    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);
    gpio_do_periodic_work();

    // Установка MOD1_RX как INPUT со значением HIGH
    utest_shared_gpio_set_input_value(MOD1, MOD_GPIO_RX, true);

    // Симуляция физического состояние пина, отличающегося от значения выхода для MOD1_TX
    // (должно игнорироваться, поскольку это выход)
    utest_shared_gpio_set_input_value(MOD1, MOD_GPIO_TX, false);

    gpio_do_periodic_work();

    // Читаем обратно GPIO_CTRL
    bool result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_CTRL");

    // MOD1_RX (INPUT) должен отражать состояние входа (HIGH)
    TEST_ASSERT_TRUE_MESSAGE(gpio_ctrl.gpio_ctrl & BIT(EC_EXT_GPIO_MOD1_RX), "MOD1_RX input should be HIGH");

    // MOD1_TX (OUTPUT) должен сохранить свое значение выхода (HIGH), а не читать с входа
    TEST_ASSERT_TRUE_MESSAGE(gpio_ctrl.gpio_ctrl & BIT(EC_EXT_GPIO_MOD1_TX), "MOD1_TX should keep output value HIGH");
}

// Сценарий: Чтение состояний GPIO, когда один пин в режиме AF
// Ожидается: бит gpio_ctrl пина в режиме AF очищен, не читается с входа
static void test_gpio_collect_ignores_af_pins(void)
{
    LOG_INFO("Testing that collect_gpio_states ignores AF mode pins");

    gpio_init();
    gpio_reset();

    // Установка MOD1_RX в режим UART
    struct REGMAP_GPIO_AF gpio_af;
    gpio_af.af = GPIO_AF_SET(GPIO_AF_MOD1_RX, GPIO_REGMAP_AF_UART);
    regmap_set_region_data(REGMAP_REGION_GPIO_AF, &gpio_af, sizeof(gpio_af));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_AF);
    gpio_do_periodic_work();

    // Установка состояния входа для MOD1_RX (должно игнорироваться для режима AF)
    utest_shared_gpio_set_input_value(MOD1, MOD_GPIO_RX, true);

    // Установка MOD1_TX как INPUT с HIGH (должно быть прочитано)
    utest_shared_gpio_set_input_value(MOD1, MOD_GPIO_TX, true);

    gpio_do_periodic_work();

    // Чтение GPIO_CTRL
    struct REGMAP_GPIO_CTRL gpio_ctrl;
    bool result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_CTRL");

    // MOD1_TX (INPUT в режиме GPIO) должен быть HIGH
    TEST_ASSERT_TRUE_MESSAGE(gpio_ctrl.gpio_ctrl & BIT(EC_EXT_GPIO_MOD1_TX), "MOD1_TX input should be HIGH");

    // MOD1_RX (режим AF) должен быть сброшен (вход не читается)
    TEST_ASSERT_FALSE_MESSAGE(gpio_ctrl.gpio_ctrl & BIT(EC_EXT_GPIO_MOD1_RX), "MOD1_RX should be cleared in AF mode");
}

// Сценарий: Установка регистра AF на зарезервированные значения (2 или 3)
// Ожидается: зарезервированные значения AF игнорируются, режим GPIO остается без изменений
static void test_gpio_af_ignores_reserved_values(void)
{
    LOG_INFO("Testing that undefined AF values (2, 3) are ignored");

    gpio_init();
    gpio_reset();

    // Установка MOD1_TX в режим INPUT изначально
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = 0;
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);
    gpio_do_periodic_work();

    // Проверка, что это INPUT
    enum mod_gpio_mode mode = utest_shared_gpio_get_mode(MOD1, MOD_GPIO_TX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_INPUT, mode, "MOD1_TX should be INPUT initially");

    // Установка AF в зарезервированное значение 2 (ни GPIO, ни UART)
    struct REGMAP_GPIO_AF gpio_af;
    gpio_af.af = GPIO_AF_SET(GPIO_AF_MOD1_TX, 2);  // Зарезервированное значение
    regmap_set_region_data(REGMAP_REGION_GPIO_AF, &gpio_af, sizeof(gpio_af));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_AF);
    gpio_do_periodic_work();

    // Режим должен остаться без изменений (INPUT) - зарезервированное значение AF игнорируется
    mode = utest_shared_gpio_get_mode(MOD1, MOD_GPIO_TX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_INPUT, mode, "MOD1_TX should remain INPUT when AF=2 (reserved)");

    // Проверка другого зарезервированного значения (3)
    gpio_af.af = GPIO_AF_SET(GPIO_AF_MOD1_TX, 3);  // Зарезервированное значение
    regmap_set_region_data(REGMAP_REGION_GPIO_AF, &gpio_af, sizeof(gpio_af));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_AF);
    gpio_do_periodic_work();

    // Режим все еще должен оставаться без изменений
    mode = utest_shared_gpio_get_mode(MOD1, MOD_GPIO_TX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_INPUT, mode, "MOD1_TX should remain INPUT when AF=3 (reserved)");
}
#endif

// Сценарий: Установка V_OUT включенным в regmap при нормальном напряжении V_OUT
// Ожидается: GPIO V_OUT в состоянии HIGH (включен)
static void test_v_out_control_enabled_when_power_ok(void)
{
    LOG_INFO("Testing V_OUT control - enabled when power OK");

    gpio_init();
    gpio_reset();

    // Установка V_OUT в ON в GPIO_CTRL
    struct REGMAP_GPIO_CTRL gpio_ctrl;
    gpio_ctrl.gpio_ctrl = BIT(EC_EXT_GPIO_V_OUT);
    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);

    // Установка статуса монитора напряжения V_OUT в OK
    utest_vmon_set_ch_status(VMON_CHANNEL_V_OUT, true);

    gpio_do_periodic_work();

    // Проверка, что GPIO V_OUT в состоянии HIGH
    uint32_t state = utest_gpio_get_output_state(v_out_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state, "V_OUT GPIO should be HIGH when power is OK");
}

// Сценарий: Установка V_OUT в ON в regmap при статусе монитора напряжения V_OUT НЕ OK
// Ожидается: V_OUT GPIO в LOW (отключен для безопасности)
static void test_v_out_control_disabled_when_power_not_ok(void)
{
    LOG_INFO("Testing V_OUT control - disabled when power not OK");

    gpio_init();
    gpio_reset();

    // Установка V_OUT в ON в GPIO_CTRL
    struct REGMAP_GPIO_CTRL gpio_ctrl;
    gpio_ctrl.gpio_ctrl = BIT(EC_EXT_GPIO_V_OUT);
    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);

    // Устанавливаем статус монитора напряжения V_OUT в НЕ OK
    utest_vmon_set_ch_status(VMON_CHANNEL_V_OUT, false);

    gpio_do_periodic_work();

    // Проверка, что V_OUT GPIO в состоянии LOW
    uint32_t state = utest_gpio_get_output_state(v_out_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "V_OUT GPIO should be LOW when power is not OK");
}

// Сценарий: Установка V_OUT в OFF в regmap при статусе монитора напряжения OK
// Ожидается: V_OUT GPIO в LOW (работает управление)
static void test_v_out_control_disabled_when_ctrl_off(void)
{
    LOG_INFO("Testing V_OUT control - disabled when CTRL is OFF");

    gpio_init();
    gpio_reset();

    // Устанавливаем V_OUT в OFF в GPIO_CTRL
    struct REGMAP_GPIO_CTRL gpio_ctrl;
    gpio_ctrl.gpio_ctrl = 0;  // V_OUT в OFF
    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);

    // Устанавливаем состояние монитора напряжения V_OUT в OK
    utest_vmon_set_ch_status(VMON_CHANNEL_V_OUT, true);

    gpio_do_periodic_work();

    // Проверка, что V_OUT GPIO в состоянии LOW
    uint32_t state = utest_gpio_get_output_state(v_out_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state, "V_OUT GPIO should be LOW when CTRL is OFF");
}

// Сценарий: Проверка всех комбинаций управления V_OUT и статуса питания
// Ожидается: V_OUT включен только когда ctrl_on=true И power_ok=true
static void test_v_out_control_requires_both_conditions(void)
{
    LOG_INFO("Testing V_OUT control - requires both power OK and CTRL ON");

    gpio_init();
    gpio_reset();

    // Проверка всех комбинаций
    struct {
        bool ctrl_on;
        bool power_ok;
        uint32_t expected_state;
    } test_cases[] = {
        { false, false, 0 },  // Оба OFF -> V_OUT OFF
        { false, true,  0 },  // CTRL OFF, питание OK -> V_OUT OFF
        { true,  false, 0 },  // CTRL ON, питание НЕ OK -> V_OUT OFF
        { true,  true,  1 },  // Оба ON -> V_OUT ON
    };

    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        struct REGMAP_GPIO_CTRL gpio_ctrl;
        gpio_ctrl.gpio_ctrl = test_cases[i].ctrl_on ? BIT(EC_EXT_GPIO_V_OUT) : 0;
        regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
        utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);

        utest_vmon_set_ch_status(VMON_CHANNEL_V_OUT, test_cases[i].power_ok);

        gpio_do_periodic_work();

        uint32_t state = utest_gpio_get_output_state(v_out_gpio);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(test_cases[i].expected_state, state,
            "V_OUT state mismatch in test case");
    }
}


#ifdef EC_MOD1_MOD2_GPIO_CONTROL
// Сценарий: Конфигурация нескольких GPIO с разными режимами и значениями
// Ожидается: Каждый GPIO работает независимо с правильным режимом и значением
static void test_multiple_gpios_independent(void)
{
    LOG_INFO("Testing multiple GPIOs independence");

    gpio_init();
    gpio_reset();

    // Устанавливаем MOD1_TX и MOD2_RTS как выходы, остальные как входы
    struct REGMAP_GPIO_DIR gpio_dir;
    gpio_dir.gpio_dir = BIT(EC_EXT_GPIO_MOD1_TX) | BIT(EC_EXT_GPIO_MOD2_RTS);
    regmap_set_region_data(REGMAP_REGION_GPIO_DIR, &gpio_dir, sizeof(gpio_dir));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_DIR);

    struct REGMAP_GPIO_CTRL gpio_ctrl;
    gpio_ctrl.gpio_ctrl = BIT(EC_EXT_GPIO_MOD1_TX);  // MOD1_TX HIGH, MOD2_RTS LOW
    regmap_set_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    utest_regmap_mark_region_changed(REGMAP_REGION_GPIO_CTRL);

    // Устанавливаем входы MOD1_RX и MOD2_RX в HIGH
    utest_shared_gpio_set_input_value(MOD1, MOD_GPIO_RX, true);
    utest_shared_gpio_set_input_value(MOD2, MOD_GPIO_RX, true);

    gpio_do_periodic_work();

    // Проверка MOD1_TX (выход HIGH)
    enum mod_gpio_mode mode = utest_shared_gpio_get_mode(MOD1, MOD_GPIO_TX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_OUTPUT, mode, "MOD1_TX should be OUTPUT");
    bool value = utest_shared_gpio_get_output_value(MOD1, MOD_GPIO_TX);
    TEST_ASSERT_TRUE_MESSAGE(value, "MOD1_TX should be HIGH");

    // Проверка MOD2_RTS (выход LOW)
    mode = utest_shared_gpio_get_mode(MOD2, MOD_GPIO_RTS);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_OUTPUT, mode, "MOD2_RTS should be OUTPUT");
    value = utest_shared_gpio_get_output_value(MOD2, MOD_GPIO_RTS);
    TEST_ASSERT_FALSE_MESSAGE(value, "MOD2_RTS should be LOW");

    // Проверка MOD1_RX (вход HIGH)
    mode = utest_shared_gpio_get_mode(MOD1, MOD_GPIO_RX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_INPUT, mode, "MOD1_RX should be INPUT");

    // Проверка MOD2_RX (вход HIGH)
    mode = utest_shared_gpio_get_mode(MOD2, MOD_GPIO_RX);
    TEST_ASSERT_EQUAL_MESSAGE(MOD_GPIO_MODE_INPUT, mode, "MOD2_RX should be INPUT");

    // Чтение GPIO_CTRL для проверки входов
    bool result = utest_regmap_get_region_data(REGMAP_REGION_GPIO_CTRL, &gpio_ctrl, sizeof(gpio_ctrl));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read GPIO_CTRL");
    TEST_ASSERT_TRUE_MESSAGE(gpio_ctrl.gpio_ctrl & BIT(EC_EXT_GPIO_MOD1_RX), "MOD1_RX input should be HIGH");
    TEST_ASSERT_TRUE_MESSAGE(gpio_ctrl.gpio_ctrl & BIT(EC_EXT_GPIO_MOD2_RX), "MOD2_RX input should be HIGH");
}
#endif


int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_gpio_init);
    RUN_TEST(test_gpio_reset);

#ifdef EC_MOD1_MOD2_GPIO_CONTROL
    RUN_TEST(test_gpio_direction_change_input_to_output);
    RUN_TEST(test_gpio_direction_change_output_to_input);
    RUN_TEST(test_gpio_set_output_value);
    RUN_TEST(test_gpio_read_input_value);
    RUN_TEST(test_gpio_af_mode_uart);
    RUN_TEST(test_gpio_af_mode_prevents_direction_change);
    RUN_TEST(test_gpio_output_value_preserved_on_direction_change);
    RUN_TEST(test_gpio_output_value_preserved_without_request);
    RUN_TEST(test_gpio_af_mode_clears_ctrl_bit);
    RUN_TEST(test_gpio_dir_preserves_v_out_as_output);
    RUN_TEST(test_gpio_af_switches_mode_based_on_direction);
    RUN_TEST(test_gpio_values_only_set_for_changed_pins);
    RUN_TEST(test_gpio_collect_only_reads_inputs);
    RUN_TEST(test_gpio_collect_ignores_af_pins);
    RUN_TEST(test_gpio_af_ignores_reserved_values);
    RUN_TEST(test_multiple_gpios_independent);
#endif

    RUN_TEST(test_v_out_control_enabled_when_power_ok);
    RUN_TEST(test_v_out_control_disabled_when_power_not_ok);
    RUN_TEST(test_v_out_control_disabled_when_ctrl_off);
    RUN_TEST(test_v_out_control_requires_both_conditions);

    return UNITY_END();
}
