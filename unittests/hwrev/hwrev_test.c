#include "unity.h"
#include "hwrev.h"
#include "config.h"
#include "adc.h"
#include "mcu-pwr.h"
#include "fix16.h"
#include "utest_adc.h"
#include "utest_regmap.h"
#include "utest_systick.h"
#include "utest_wbmcu_system.h"
#include "utest_mcu_pwr.h"
#include "utest_wdt_stm32.h"
#include "utest_system_led.h"
#include "regmap-structs.h"
#include "regmap-int.h"
#include <setjmp.h>

#define LOG_LEVEL LOG_LEVEL_INFO
#include "console_log.h"

// Вспомогательные функции для тестов из hwrev_test_stubs.c
bool utest_rcc_set_hsi_pll_64mhz_clock_called(void);
bool utest_spi_slave_was_init_called(void);
void utest_hwrev_stubs_reset(void);

// Колбэк для watchdog_reload - увеличивает время после первого вызова
static void watchdog_reload_callback_trigger_reset(void)
{
    // После первого вызова watchdog_reload устанавливаем время > 10000
    // чтобы NVIC_SystemReset был вызван при следующей проверке
    if (utest_watchdog_get_reload_count() == 1) {
        utest_systick_set_time_ms(10001);
    }
}

// Расчёт ожидаемого значения ADC для ревизии
#define HWREV_ADC_VALUE_EXPECTED(res_up, res_down) \
    ((res_down) * 4096 / ((res_up) + (res_down)))

#define HWREV_ADC_VALUE_MIN(res_up, res_down) \
    HWREV_ADC_VALUE_EXPECTED(res_up, res_down) - \
    (HWREV_ADC_VALUE_EXPECTED(res_up, res_down) * WBEC_HWREV_DIFF_PERCENT / 100) - \
    WBEC_HWREV_DIFF_ADC

#define HWREV_ADC_VALUE_MAX(res_up, res_down) \
    HWREV_ADC_VALUE_EXPECTED(res_up, res_down) + \
    (HWREV_ADC_VALUE_EXPECTED(res_up, res_down) * WBEC_HWREV_DIFF_PERCENT / 100) + \
    WBEC_HWREV_DIFF_ADC

// Вспомогательный макрос для получения кода ревизии
#define __HWREV_CODE(hwrev_name, hwrev_code, res_up, res_down) hwrev_code,
static const uint16_t hwrev_codes[HWREV_COUNT] = {
    WBEC_HWREV_DESC(__HWREV_CODE)
};

// Вспомогательный макрос для получения значений ADC
#define __HWREV_ADC_VALUES(hwrev_name, hwrev_code, res_up, res_down) \
    HWREV_ADC_VALUE_EXPECTED(res_up, res_down),
static const int16_t hwrev_adc_values[HWREV_COUNT] = {
    WBEC_HWREV_DESC(__HWREV_ADC_VALUES)
};

// Вспомогательный макрос для получения минимальных значений ADC
#define __HWREV_ADC_MIN_VALUES(hwrev_name, hwrev_code, res_up, res_down) \
    HWREV_ADC_VALUE_MIN(res_up, res_down),
static const int16_t hwrev_adc_min_values[HWREV_COUNT] = {
    WBEC_HWREV_DESC(__HWREV_ADC_MIN_VALUES)
};

// Вспомогательный макрос для получения максимальных значений ADC
#define __HWREV_ADC_MAX_VALUES(hwrev_name, hwrev_code, res_up, res_down) \
    HWREV_ADC_VALUE_MAX(res_up, res_down),
static const int16_t hwrev_adc_max_values[HWREV_COUNT] = {
    WBEC_HWREV_DESC(__HWREV_ADC_MAX_VALUES)
};

void setUp(void)
{
    // Сброс состояний моков
    utest_regmap_reset();
    utest_systick_set_time_ms(0);
    utest_systick_reset_init_flag();
    utest_nvic_reset();
    utest_mcu_reset();
    utest_watchdog_reset();
    utest_system_led_reset();
    utest_hwrev_stubs_reset();
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
}

void tearDown(void)
{

}

// Сценарий: Получение ревизии железа до инициализации
// Ожидается: hwrev_get() возвращает HWREV_UNKNOWN
static void test_hwrev_get_default(void)
{
    LOG_INFO("Testing hwrev_get default value");

    // До инициализации hwrev должен быть HWREV_UNKNOWN
    enum hwrev rev = hwrev_get();
    TEST_ASSERT_EQUAL_MESSAGE(HWREV_UNKNOWN, rev, "Before initialization, hwrev should be HWREV_UNKNOWN");
}

// Сценарий: Инициализация hwrev со значением ADC, соответствующим текущей модели при POWER_ON
// Ожидается: hwrev_get() возвращает WBEC_HWREV (обнаружено правильное железо)
static void test_hwrev_init(void)
{
    LOG_INFO("Testing hwrev init for current model");

    // Устанавливаем значение ADC, соответствующее текущему железу
    utest_adc_set_ch_raw(ADC_CHANNEL_ADC_HW_VER, fix16_from_int(hwrev_adc_values[WBEC_HWREV]));

    hwrev_init_and_check();

    enum hwrev rev = hwrev_get();
    TEST_ASSERT_EQUAL_MESSAGE(WBEC_HWREV, rev, "After initialization, hwrev should match WBEC_HWREV");
}

// Сценарий: Инициализация hwrev с причиной не POWER_ON (например, RTC_ALARM)
// Ожидается: проверка hwrev пропущена, hwrev установлен в WBEC_HWREV, нет флага ошибки
static void test_hwrev_init_non_poweron(void)
{
    LOG_INFO("Testing hwrev init with non-POWER_ON reason (skip hwrev check)");

    // Устанавливаем причину включения в RTC_ALARM (не POWER_ON)
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_RTC_ALARM);

    // Устанавливаем значение ADC для НЕПРАВИЛЬНОГО железа, чтобы проверить, что проверка hwrev пропущена
#ifdef MODEL_WB74
    enum hwrev wrong_hwrev = HWREV_WB85;
#elif defined(MODEL_WB85)
    enum hwrev wrong_hwrev = HWREV_WB74;
#else
    #error "Unknown model"
#endif
    utest_adc_set_ch_raw(ADC_CHANNEL_ADC_HW_VER, fix16_from_int(hwrev_adc_values[wrong_hwrev]));

    // Вызываем hwrev_init_and_check - должна пропустить проверку hwrev и просто установить WBEC_HWREV
    hwrev_init_and_check();

    // Проверяем, что hwrev был установлен в WBEC_HWREV (без проверки по ADC)
    enum hwrev rev = hwrev_get();
    TEST_ASSERT_EQUAL_MESSAGE(WBEC_HWREV, rev, "With non-POWER_ON reason, hwrev should be set to WBEC_HWREV without checking ADC");

    // Читаем HW_INFO_PART1 для проверки, что регистры заполнены правильно
    struct REGMAP_HW_INFO_PART1 hw_info_1;
    bool result = utest_regmap_get_region_data(REGMAP_REGION_HW_INFO_PART1, &hw_info_1, sizeof(hw_info_1));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read HW_INFO_PART1 region");

    // Проверяем, что hwrev_code установлен в правильный код модели (а не ошибку)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(hwrev_codes[WBEC_HWREV], hw_info_1.hwrev_code, "hwrev_code should match current model code");

    // Проверяем, что hwrev_error_flag = 0 (нет ошибки)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, hw_info_1.hwrev_error_flag, "hwrev_error_flag should be 0 when hwrev check is skipped");

    // Читаем HW_INFO_PART2
    struct REGMAP_HW_INFO_PART2 hw_info_2;
    result = utest_regmap_get_region_data(REGMAP_REGION_HW_INFO_PART2, &hw_info_2, sizeof(hw_info_2));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read HW_INFO_PART2 region");

    // Проверяем, что hwrev_ok установлен в WBEC_ID (успех)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(WBEC_ID, hw_info_2.hwrev_ok, "hwrev_ok should be WBEC_ID when hwrev check is skipped");
}

// Сценарий: Запись информации о железе в regmap, когда обнаруженный hwrev правильный
// Ожидается: Regmap заполнен правильным WBEC_ID, кодом hwrev, fwrev, UID;
// hwrev_error_flag=0, hwrev_ok=WBEC_ID
static void test_hwrev_put_hw_info_to_regmap_correct(void)
{
    LOG_INFO("Testing hwrev_put_hw_info_to_regmap for correct revision");

    // Устанавливаем значение ADC, соответствующее текущему железу
    utest_adc_set_ch_raw(ADC_CHANNEL_ADC_HW_VER, fix16_from_int(hwrev_adc_values[WBEC_HWREV]));

    hwrev_init_and_check();
    hwrev_put_hw_info_to_regmap();

    // Читаем HW_INFO_PART1
    struct REGMAP_HW_INFO_PART1 hw_info_1;
    bool result = utest_regmap_get_region_data(REGMAP_REGION_HW_INFO_PART1, &hw_info_1, sizeof(hw_info_1));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read HW_INFO_PART1 region");

    // Проверяем WBEC_ID
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(WBEC_ID, hw_info_1.wbec_id, "wbec_id should match WBEC_ID");

    // Проверяем hwrev_code
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(hwrev_codes[WBEC_HWREV], hw_info_1.hwrev_code, "hwrev_code should match current model code");

    // Проверяем поля fwrev
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(2, hw_info_1.fwrev_major, "fwrev_major should be 2");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3, hw_info_1.fwrev_minor, "fwrev_minor should be 3");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(5, hw_info_1.fwrev_patch, "fwrev_patch should be 5");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(7, hw_info_1.fwrev_suffix, "fwrev_suffix should be 7");

    // Проверяем hwrev_error_flag (должен быть 0 для правильной ревизии)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, hw_info_1.hwrev_error_flag, "hwrev_error_flag should be 0 for correct revision");

    // Читаем HW_INFO_PART2
    struct REGMAP_HW_INFO_PART2 hw_info_2;
    result = utest_regmap_get_region_data(REGMAP_REGION_HW_INFO_PART2, &hw_info_2, sizeof(hw_info_2));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read HW_INFO_PART2 region");

    // Проверяем uid (из UID_BASE)
    TEST_ASSERT_EQUAL_UINT16_ARRAY_MESSAGE((uint16_t *)UID_BASE, hw_info_2.uid, 6, "uid array should match UID_BASE");

    // Проверяем hwrev_ok (должен быть WBEC_ID для правильной ревизии)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(WBEC_ID, hw_info_2.hwrev_ok, "hwrev_ok should be WBEC_ID for correct revision");
}

// Сценарий: Запись информации о железе в regmap, когда обнаруженный hwrev не соответствует прошивке
// Ожидается: Regmap заполнен обнаруженным кодом hwrev, hwrev_error_flag=0b1010,
// hwrev_ok=0; вход в бесконечный цикл с миганием LED; вызов NVIC_SystemReset
static void test_hwrev_put_hw_info_to_regmap_incorrect(void)
{
    LOG_INFO("Testing hwrev_put_hw_info_to_regmap for incorrect revision");

    // Устанавливаем значение ADC, соответствующее другому железу (противоположное текущей модели)
#ifdef MODEL_WB74
    enum hwrev wrong_hwrev = HWREV_WB85;
#elif defined(MODEL_WB85)
    enum hwrev wrong_hwrev = HWREV_WB74;
#else
    #error "Unknown model"
#endif

    utest_adc_set_ch_raw(ADC_CHANNEL_ADC_HW_VER, fix16_from_int(hwrev_adc_values[wrong_hwrev]));

    // Вызываем hwrev_init_and_check() с POWER_ON для проверки реального поведения
    // Войдёт в бесконечный цикл, поэтому используем setjmp/longjmp для выхода, когда вызовется NVIC_SystemReset
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);

    // Начинаем со времени < 10 секунд, чтобы разрешить вызов watchdog_reload
    utest_systick_set_time_ms(5000);

    // Устанавливаем колбэк, который установит время > 10000 после первого watchdog_reload
    utest_watchdog_set_reload_callback(watchdog_reload_callback_trigger_reset);

    // Используем setjmp/longjmp для выхода из бесконечного цикла
    jmp_buf exit_jmp;
    utest_nvic_set_exit_jmp(&exit_jmp);

    if (setjmp(exit_jmp) == 0) {
        // Это обнаружит несоответствие hwrev, вызовет hwrev_put_hw_info_to_regmap(),
        // войдёт в бесконечный цикл и затем вызовет NVIC_SystemReset
        hwrev_init_and_check();

        // Не должны сюда попасть
        TEST_FAIL_MESSAGE("Should not reach this point - NVIC_SystemReset should be called");
    }

    // longjmp вернул нас сюда после вызова NVIC_SystemReset
    // Теперь можем проверить, что regmap был заполнен правильно
    utest_nvic_set_exit_jmp(NULL);

    // Проверяем, что функции инициализации были вызваны во время обработки несоответствия hwrev
    TEST_ASSERT_TRUE_MESSAGE(utest_rcc_set_hsi_pll_64mhz_clock_called(), "rcc_set_hsi_pll_64mhz_clock should be called on hwrev mismatch");
    TEST_ASSERT_TRUE_MESSAGE(utest_systick_was_init_called(), "systick_init should be called on hwrev mismatch");
    TEST_ASSERT_TRUE_MESSAGE(utest_spi_slave_was_init_called(), "spi_slave_init should be called on hwrev mismatch");
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_was_init_called(), "regmap_init should be called on hwrev mismatch");

    // Проверяем, что watchdog_reload был вызван в бесконечном цикле
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, utest_watchdog_get_reload_count(), "watchdog_reload should be called at least once in the mismatch loop");

    // Проверяем, что system_led_do_periodic_work был вызван в бесконечном цикле
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, utest_system_led_get_periodic_work_count(), "system_led_do_periodic_work should be called at least once in the mismatch loop");

    // Проверяем, что LED установлен в режим мигания с правильными параметрами
    TEST_ASSERT_EQUAL_MESSAGE(UTEST_LED_MODE_BLINK, utest_system_led_get_mode(), "LED should be in BLINK mode on hwrev mismatch");
    uint16_t on_ms = 0, off_ms = 0;
    utest_system_led_get_blink_params(&on_ms, &off_ms);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(25, on_ms, "LED blink on time should be 25ms");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(25, off_ms, "LED blink off time should be 25ms");

    // Читаем HW_INFO_PART1
    struct REGMAP_HW_INFO_PART1 hw_info_1;
    bool result = utest_regmap_get_region_data(REGMAP_REGION_HW_INFO_PART1, &hw_info_1, sizeof(hw_info_1));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read HW_INFO_PART1 region");

    // Проверяем WBEC_ID
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(WBEC_ID, hw_info_1.wbec_id, "wbec_id should match WBEC_ID");

    // При обнаружении несоответствия hwrev, hwrev_code должен быть установлен в обнаруженный (неправильный) hwrev
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(hwrev_codes[wrong_hwrev], hw_info_1.hwrev_code, "hwrev_code should match detected (wrong) hwrev code");

    // Проверяем поля fwrev
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(2, hw_info_1.fwrev_major, "fwrev_major should be 2");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3, hw_info_1.fwrev_minor, "fwrev_minor should be 3");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(5, hw_info_1.fwrev_patch, "fwrev_patch should be 5");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(7, hw_info_1.fwrev_suffix, "fwrev_suffix should be 7");

    // Проверяем hwrev_error_flag (должен быть 0b1010 для несовпадения hwrev)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0b1010, hw_info_1.hwrev_error_flag, "hwrev_error_flag should be 0b1010 for hwrev mismatch");

    // Читаем HW_INFO_PART2
    struct REGMAP_HW_INFO_PART2 hw_info_2;
    result = utest_regmap_get_region_data(REGMAP_REGION_HW_INFO_PART2, &hw_info_2, sizeof(hw_info_2));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read HW_INFO_PART2 region");

    // Проверяем uid (из UID_BASE)
    TEST_ASSERT_EQUAL_UINT16_ARRAY_MESSAGE((uint16_t *)UID_BASE, hw_info_2.uid, 6, "uid array should match UID_BASE");

    // Проверяем hwrev_ok (должен быть 0 для неправильной ревизии, не WBEC_ID)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, hw_info_2.hwrev_ok, "hwrev_ok should be 0 for incorrect revision");
}

// Сценарий: Инициализация hwrev со значением ADC, не соответствующим ни одной известной ревизии
// Ожидается: hwrev остаётся HWREV_UNKNOWN, hwrev_error_flag=0b1010, hwrev_ok=0;
// вход в бесконечный цикл с миганием LED; вызов NVIC_SystemReset
static void test_hwrev_unknown_adc_value(void)
{
    LOG_INFO("Testing hwrev with unknown ADC value");

    // Устанавливаем значение ADC, не соответствующее ни одной известной ревизии
    // WB74: expected=0, range=-10..10
    // WB85: expected=738, range=706..770
    // 2000 находится вне любого известного диапазона
    utest_adc_set_ch_raw(ADC_CHANNEL_ADC_HW_VER, fix16_from_int(2000));

    // Вызываем hwrev_init_and_check() с POWER_ON для проверки реального поведения с неизвестным значением ADC
    // Функция обнаружит неизвестный hwrev, войдёт в бесконечный цикл, поэтому используем setjmp/longjmp для выхода
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);

    // Начинаем с времени < 10 секунд, чтобы watchdog_reload мог быть вызван
    utest_systick_set_time_ms(5000);

    // Устанавливаем callback, который переведёт время > 10000 после первого watchdog_reload
    utest_watchdog_set_reload_callback(watchdog_reload_callback_trigger_reset);

    // Используем setjmp/longjmp для выхода из бесконечного цикла
    jmp_buf exit_jmp;
    utest_nvic_set_exit_jmp(&exit_jmp);

    if (setjmp(exit_jmp) == 0) {
        // Это обнаружит неизвестный hwrev (не соответствует ни одной известной ревизии),
        // вызовет hwrev_put_hw_info_to_regmap(), войдёт в бесконечный цикл, и вызовет NVIC_SystemReset
        hwrev_init_and_check();

        // Не должны попасть сюда
        TEST_FAIL_MESSAGE("Should not reach this point - NVIC_SystemReset should be called");
    }

    // longjmp вернул нас сюда после вызова NVIC_SystemReset
    utest_nvic_set_exit_jmp(NULL);

    // Проверяем, что функции инициализации были вызваны при обработке несовпадения hwrev
    TEST_ASSERT_TRUE_MESSAGE(utest_rcc_set_hsi_pll_64mhz_clock_called(), "rcc_set_hsi_pll_64mhz_clock should be called on unknown hwrev");
    TEST_ASSERT_TRUE_MESSAGE(utest_systick_was_init_called(), "systick_init should be called on unknown hwrev");
    TEST_ASSERT_TRUE_MESSAGE(utest_spi_slave_was_init_called(), "spi_slave_init should be called on unknown hwrev");
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_was_init_called(), "regmap_init should be called on unknown hwrev");

    // Проверяем, что watchdog_reload был вызван в бесконечном цикле
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, utest_watchdog_get_reload_count(), "watchdog_reload should be called at least once in the unknown hwrev loop");

    // Проверяем, что system_led_do_periodic_work был вызван в бесконечном цикле
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, utest_system_led_get_periodic_work_count(), "system_led_do_periodic_work should be called at least once in the unknown hwrev loop");

    // Проверяем, что LED установлен в режим мигания с правильными параметрами
    TEST_ASSERT_EQUAL_MESSAGE(UTEST_LED_MODE_BLINK, utest_system_led_get_mode(), "LED should be in BLINK mode on unknown hwrev");
    uint16_t on_ms = 0, off_ms = 0;
    utest_system_led_get_blink_params(&on_ms, &off_ms);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(25, on_ms, "LED blink on time should be 25ms");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(25, off_ms, "LED blink off time should be 25ms");

    // Проверяем, что hwrev остался HWREV_UNKNOWN (не найдена подходящая ревизия)
    enum hwrev rev = hwrev_get();
    TEST_ASSERT_EQUAL_MESSAGE(HWREV_UNKNOWN, rev, "With unknown ADC value, hwrev should remain HWREV_UNKNOWN");

    // Читаем HW_INFO_PART1 для проверки флагов ошибок
    struct REGMAP_HW_INFO_PART1 hw_info_1;
    bool result = utest_regmap_get_region_data(REGMAP_REGION_HW_INFO_PART1, &hw_info_1, sizeof(hw_info_1));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read HW_INFO_PART1 region");

    // Проверяем WBEC_ID
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(WBEC_ID, hw_info_1.wbec_id, "wbec_id should match WBEC_ID");

    // Проверяем hwrev_code (должен быть HWREV_UNKNOWN, обрезанный до 12 бит)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE((HWREV_UNKNOWN & 0xFFF), hw_info_1.hwrev_code, "hwrev_code should be HWREV_UNKNOWN truncated to 12 bits for unknown hwrev");

    // Проверяем поля fwrev
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(2, hw_info_1.fwrev_major, "fwrev_major should be 2");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3, hw_info_1.fwrev_minor, "fwrev_minor should be 3");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(5, hw_info_1.fwrev_patch, "fwrev_patch should be 5");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(7, hw_info_1.fwrev_suffix, "fwrev_suffix should be 7");

    // Проверяем hwrev_error_flag (должен быть 0b1010 для несовпадения/неизвестного hwrev)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0b1010, hw_info_1.hwrev_error_flag, "hwrev_error_flag should be 0b1010 for unknown hwrev");

    // Читаем HW_INFO_PART2
    struct REGMAP_HW_INFO_PART2 hw_info_2;
    result = utest_regmap_get_region_data(REGMAP_REGION_HW_INFO_PART2, &hw_info_2, sizeof(hw_info_2));
    TEST_ASSERT_TRUE_MESSAGE(result, "Should be able to read HW_INFO_PART2 region");

    // Проверяем uid (из UID_BASE)
    TEST_ASSERT_EQUAL_UINT16_ARRAY_MESSAGE((uint16_t *)UID_BASE, hw_info_2.uid, 6, "uid array should match UID_BASE");

    // Проверяем hwrev_ok (должен быть 0 для неизвестной ревизии, не WBEC_ID)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, hw_info_2.hwrev_ok, "hwrev_ok should be 0 for unknown revision");
}

// Сценарий: Обнаружение несовпадения hwrev и проверка, что вызывается NVIC_SystemReset
// Ожидается: Функции инициализации вызваны, вход в бесконечный цикл,
// NVIC_SystemReset вызван через 10 секунд
static void test_hwrev_nvic_reset_on_mismatch(void)
{
    LOG_INFO("Testing NVIC_SystemReset call on hwrev mismatch");

    // Устанавливаем POWER_ON, чтобы проверка hwrev была активна
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);

    // Устанавливаем значение ADC для другой модели (несовпадение)
#ifdef MODEL_WB74
    enum hwrev wrong_hwrev = HWREV_WB85;
#elif defined(MODEL_WB85)
    enum hwrev wrong_hwrev = HWREV_WB74;
#else
    #error "Unknown model"
#endif

    utest_adc_set_ch_raw(ADC_CHANNEL_ADC_HW_VER, fix16_from_int(hwrev_adc_values[wrong_hwrev]));

    // Начинаем с времени < 10 секунд, чтобы watchdog_reload мог быть вызван
    utest_systick_set_time_ms(5000);

    // Устанавливаем callback, который переведёт время > 10000 после первого watchdog_reload
    utest_watchdog_set_reload_callback(watchdog_reload_callback_trigger_reset);

    // Проверяем, что NVIC_SystemReset не был вызван до hwrev_init_and_check()
    TEST_ASSERT_FALSE_MESSAGE(utest_nvic_was_reset_called(), "NVIC_SystemReset should not be called before hwrev_init_and_check");

    // Используем setjmp/longjmp для выхода из бесконечного цикла при вызове NVIC_SystemReset
    jmp_buf exit_jmp;
    utest_nvic_set_exit_jmp(&exit_jmp);

    if (setjmp(exit_jmp) == 0) {
        // Вызываем инициализацию - она войдёт в цикл несовпадения hwrev
        // и сразу вызовет NVIC_SystemReset, так как время уже > 10 секунд
        hwrev_init_and_check();

        // Не должны попасть сюда
        TEST_FAIL_MESSAGE("Should not reach this point - NVIC_SystemReset should be called");
    } else {
        // longjmp вернул управление сюда, что означает, что NVIC_SystemReset был вызван
        // Проверяем, что NVIC_SystemReset действительно был вызван
        TEST_ASSERT_TRUE_MESSAGE(utest_nvic_was_reset_called(), "NVIC_SystemReset should be called when hwrev mismatch detected");

        // Проверяем, что функции инициализации были вызваны при обработке несовпадения hwrev
        TEST_ASSERT_TRUE_MESSAGE(utest_rcc_set_hsi_pll_64mhz_clock_called(), "rcc_set_hsi_pll_64mhz_clock should be called on hwrev mismatch");
        TEST_ASSERT_TRUE_MESSAGE(utest_systick_was_init_called(), "systick_init should be called on hwrev mismatch");
        TEST_ASSERT_TRUE_MESSAGE(utest_spi_slave_was_init_called(), "spi_slave_init should be called on hwrev mismatch");
        TEST_ASSERT_TRUE_MESSAGE(utest_regmap_was_init_called(), "regmap_init should be called on hwrev mismatch");

        // Проверяем, что watchdog_reload был вызван в бесконечном цикле
        TEST_ASSERT_GREATER_THAN_MESSAGE(0, utest_watchdog_get_reload_count(), "watchdog_reload should be called at least once in the mismatch loop");

        // Проверяем, что system_led_do_periodic_work был вызван в бесконечном цикле
        TEST_ASSERT_GREATER_THAN_MESSAGE(0, utest_system_led_get_periodic_work_count(), "system_led_do_periodic_work should be called at least once in the mismatch loop");

        // Проверяем, что LED установлен в режим мигания с правильными параметрами
        TEST_ASSERT_EQUAL_MESSAGE(UTEST_LED_MODE_BLINK, utest_system_led_get_mode(), "LED should be in BLINK mode on hwrev mismatch");
        uint16_t on_ms = 0, off_ms = 0;
        utest_system_led_get_blink_params(&on_ms, &off_ms);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(25, on_ms, "LED blink on time should be 25ms");
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(25, off_ms, "LED blink off time should be 25ms");
    }
}

// Сценарий: Обнаружение hwrev со значением ADC на минимальной границе допустимого диапазона
// Ожидается: hwrev правильно обнаружен как WBEC_HWREV на границе adc_min
static void test_hwrev_adc_range_min_boundary(void)
{
    LOG_INFO("Testing hwrev detection at adc_min boundary");

    // Устанавливаем значение ADC ровно в adc_min для текущего железа
    utest_adc_set_ch_raw(ADC_CHANNEL_ADC_HW_VER, fix16_from_int(hwrev_adc_min_values[WBEC_HWREV]));

    hwrev_init_and_check();

    // Должен быть правильно обнаружен hwrev на минимальной границе
    enum hwrev rev = hwrev_get();
    TEST_ASSERT_EQUAL_MESSAGE(WBEC_HWREV, rev, "hwrev should be detected at adc_min boundary");
}

// Сценарий: Обнаружение hwrev со значением ADC на максимальной границе допустимого диапазона
// Ожидается: hwrev правильно обнаружен как WBEC_HWREV на границе adc_max
static void test_hwrev_adc_range_max_boundary(void)
{
    LOG_INFO("Testing hwrev detection at adc_max boundary");

    // Устанавливаем значение ADC ровно в adc_max для текущего железа
    utest_adc_set_ch_raw(ADC_CHANNEL_ADC_HW_VER, fix16_from_int(hwrev_adc_max_values[WBEC_HWREV]));

    hwrev_init_and_check();

    // Должен быть правильно обнаружен hwrev на максимальной границе
    enum hwrev rev = hwrev_get();
    TEST_ASSERT_EQUAL_MESSAGE(WBEC_HWREV, rev, "hwrev should be detected at adc_max boundary");
}

// Сценарий: Обнаружение hwrev со значением ADC ниже минимального допустимого диапазона
// Ожидается: hwrev остаётся HWREV_UNKNOWN, вход в цикл ошибки
static void test_hwrev_adc_range_below_min(void)
{
    LOG_INFO("Testing hwrev detection below adc_min boundary");

    // Устанавливаем значение ADC ниже adc_min для текущего железа
    // Это значение не должно соответствовать ни одной известной ревизии
    int16_t test_value = hwrev_adc_min_values[WBEC_HWREV] - 1;

    // Убеждаемся, что значение не попадает в диапазон другой ревизии
    for (int i = 0; i < HWREV_COUNT; i++) {
        if (i != WBEC_HWREV && test_value >= hwrev_adc_min_values[i] && test_value <= hwrev_adc_max_values[i]) {
            TEST_FAIL_MESSAGE("Test value falls into another revision's range, test is invalid");
            return;
        }
    }

    utest_adc_set_ch_raw(ADC_CHANNEL_ADC_HW_VER, fix16_from_int(test_value));

    // Вызываем hwrev_init_and_check() - должна обнаружить неизвестный hwrev и войти в бесконечный цикл
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_systick_set_time_ms(5000);
    utest_watchdog_set_reload_callback(watchdog_reload_callback_trigger_reset);

    jmp_buf exit_jmp;
    utest_nvic_set_exit_jmp(&exit_jmp);

    if (setjmp(exit_jmp) == 0) {
        hwrev_init_and_check();
        TEST_FAIL_MESSAGE("Should not reach this point - NVIC_SystemReset should be called");
    }

    utest_nvic_set_exit_jmp(NULL);

    // Не должен быть обнаружен hwrev (остаётся HWREV_UNKNOWN)
    enum hwrev rev = hwrev_get();
    TEST_ASSERT_EQUAL_MESSAGE(HWREV_UNKNOWN, rev, "hwrev should remain HWREV_UNKNOWN when ADC value is below adc_min");
}

// Сценарий: Обнаружение hwrev со значением ADC выше максимального допустимого диапазона
// Ожидается: hwrev остаётся HWREV_UNKNOWN, вход в цикл ошибки
static void test_hwrev_adc_range_above_max(void)
{
    LOG_INFO("Testing hwrev detection above adc_max boundary");

    // Устанавливаем значение ADC выше adc_max для текущего железа
    // Это значение не должно соответствовать ни одной известной ревизии
    int16_t test_value = hwrev_adc_max_values[WBEC_HWREV] + 1;

    // Убеждаемся, что значение не попадает в диапазон другой ревизии
    for (int i = 0; i < HWREV_COUNT; i++) {
        if (i != WBEC_HWREV && test_value >= hwrev_adc_min_values[i] && test_value <= hwrev_adc_max_values[i]) {
            TEST_FAIL_MESSAGE("Test value falls into another revision's range, test is invalid");
            return;
        }
    }

    utest_adc_set_ch_raw(ADC_CHANNEL_ADC_HW_VER, fix16_from_int(test_value));

    // Вызываем hwrev_init_and_check() - должна обнаружить неизвестный hwrev и войти в бесконечный цикл
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_systick_set_time_ms(5000);
    utest_watchdog_set_reload_callback(watchdog_reload_callback_trigger_reset);

    jmp_buf exit_jmp;
    utest_nvic_set_exit_jmp(&exit_jmp);

    if (setjmp(exit_jmp) == 0) {
        hwrev_init_and_check();
        TEST_FAIL_MESSAGE("Should not reach this point - NVIC_SystemReset should be called");
    }

    utest_nvic_set_exit_jmp(NULL);

    // Не должен быть обнаружен hwrev как WBEC_HWREV
    enum hwrev rev = hwrev_get();
    TEST_ASSERT_EQUAL_MESSAGE(HWREV_UNKNOWN, rev, "hwrev should remain HWREV_UNKNOWN when ADC value is above adc_max");
}


int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_hwrev_get_default);
    RUN_TEST(test_hwrev_unknown_adc_value);     // Должен выполняться в начале, пока hwrev ещё HWREV_UNKNOWN
    RUN_TEST(test_hwrev_adc_range_below_min);   // Должен выполняться в начале, пока hwrev ещё HWREV_UNKNOWN
    RUN_TEST(test_hwrev_adc_range_above_max);   // Должен выполняться в начале, пока hwrev ещё HWREV_UNKNOWN
    RUN_TEST(test_hwrev_init);
    RUN_TEST(test_hwrev_init_non_poweron);
    RUN_TEST(test_hwrev_adc_range_min_boundary);
    RUN_TEST(test_hwrev_adc_range_max_boundary);
    RUN_TEST(test_hwrev_put_hw_info_to_regmap_correct);
    RUN_TEST(test_hwrev_put_hw_info_to_regmap_incorrect);
    RUN_TEST(test_hwrev_nvic_reset_on_mismatch);

    return UNITY_END();
}
