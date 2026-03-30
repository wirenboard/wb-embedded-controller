#include "unity.h"
#include "pwrkey.h"
#include "config.h"
#include "systick.h"
#include "utest_systick.h"
#include "utest_wbmcu_system.h"
#include "utest_gpio.h"

#define LOG_LEVEL LOG_LEVEL_INFO
#include "console_log.h"

// Пин PWRKEY (как определено в config)
static const gpio_pin_t pwrkey_gpio = { EC_GPIO_PWRKEY };

void setUp(void)
{
    // Сброс всех состояний моков
    utest_gpio_reset_instances();
    utest_systick_set_time_ms(1000);
    utest_pwr_reset();

    // Очищаем все ожидающие флаги нажатий, вызывая обработчики
    // Это необходимо, потому что pwrkey.c использует статические переменные,
    // которые сохраняются между тестами
    pwrkey_handle_short_press();
    pwrkey_handle_long_press();
}

void tearDown(void)
{
}

static void simulate_button_press(void)
{
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 0);  // Low = нажата
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 1);  // High = нажата
    #endif
}

static void simulate_button_release(void)
{
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        utest_gpio_set_input_state(pwrkey_gpio, 1);  // High = отпущена
    #else
        utest_gpio_set_input_state(pwrkey_gpio, 0);  // Low = отпущена
    #endif
}

static void simulate_button_press_with_debounce(void)
{
    simulate_button_press();

    // Запускаем периодическую работу с нажатием
    pwrkey_do_periodic_work();

    // Ждём антидребезга нажатия
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS + 1);
    pwrkey_do_periodic_work();
}

static void simulate_button_release_with_debounce(void)
{
    simulate_button_release();

    // Запускаем периодическую работу
    pwrkey_do_periodic_work();

    // Ждём антидребезга
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS + 1);
    pwrkey_do_periodic_work();
}

// Сценарий: Инициализация подсистемы кнопки питания
// Ожидается: PWRKEY GPIO сконфигурирован как вход с соответствующей подтяжкой и
// источник пробуждения включён в регистрах PWR
static void test_pwrkey_init(void)
{
    LOG_INFO("Testing pwrkey initialization");

    pwrkey_init();

    // Проверяем, что PWRKEY GPIO сконфигурирован как вход
    uint32_t mode = utest_gpio_get_mode(pwrkey_gpio);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(GPIO_MODE_INPUT, mode, "PWRKEY GPIO should be configured as INPUT");

    // Проверяем, что регистры PWR сконфигурированы правильно
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        // Проверяем регистр управления подтяжкой для порта A
        uint32_t pucra_expected = (1U << pwrkey_gpio.pin);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(pucra_expected, PWR->PUCRA, "PWR->PUCRA should have pull-up set for PWRKEY pin");

        // Проверяем выставление триггера по заднему фронту в регистре CR4
        uint32_t cr4_expected = (1U << (EC_GPIO_PWRKEY_WKUP_NUM - 1));
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(cr4_expected, PWR->CR4, "PWR->CR4 should have falling edge trigger set");
    #elif defined EC_GPIO_PWRKEY_ACTIVE_HIGH
        // Проверяем регистр управления pull-down для порта A
        uint32_t pdcra_expected = (1U << pwrkey_gpio.pin);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(pdcra_expected, PWR->PDCRA, "PWR->PDCRA should have pull-down set for PWRKEY pin");
    #endif

    // Проверяем источник пробуждения в CR3
    uint32_t cr3_expected = (1U << (EC_GPIO_PWRKEY_WKUP_NUM - 1));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(cr3_expected, PWR->CR3, "PWR->CR3 should have wakeup source set for PWRKEY");
}

// Сценарий: Ожидание периода подавления дребезга кнопки питания после инициализации
// Ожидается: pwrkey_ready() возвращает false до подавления дребезга, true после подавления дребезга
static void test_pwrkey_ready_after_debounce(void)
{
    LOG_INFO("Testing pwrkey ready state after debounce");

    pwrkey_init();

    // Эмулируем отпущенную кнопку (active low)
    simulate_button_release();

    // Запускаем периодическую работу для регистрации начального состояния
    pwrkey_do_periodic_work();

    // До истечения времени подавления дребезга, pwrkey не должен быть готов
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_ready(), "pwrkey should not be ready before debounce time");

    // Продвигаем время, но недостаточно для подавления дребезга
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS);
    pwrkey_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_ready(), "pwrkey should not be ready before debounce time elapsed");

    // Продвигаем время, чтобы завершить подавление дребезга
    utest_systick_advance_time_ms(1);
    pwrkey_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(pwrkey_ready(), "pwrkey should be ready after debounce time elapsed");
}

// Сценарий: Нажатие и отпускание кнопки питания
// Ожидается: pwrkey_pressed() возвращает false когда кнопка отпущена, true когда нажата
static void test_pwrkey_pressed_state(void)
{
    LOG_INFO("Testing pwrkey pressed state detection");

    pwrkey_init();

    // Симуляция отпускания кнопки
    simulate_button_release_with_debounce();

    TEST_ASSERT_FALSE_MESSAGE(pwrkey_pressed(), "pwrkey_pressed() should return false when button is released");

    // Симуляция нажатия кнопки
    simulate_button_press_with_debounce();

    TEST_ASSERT_TRUE_MESSAGE(pwrkey_pressed(), "pwrkey_pressed() should return true when button is pressed");
}

// Сценарий: Нажатие кнопки и измерение задержки подавления дребезга
// Ожидается: Кнопка не регистрируется как нажатая, пока не пройдёт время подавления дребезга
static void test_pwrkey_debounce_on_press(void)
{
    LOG_INFO("Testing debounce on button press");

    pwrkey_init();

    // Начинаем с отпущенной кнопки
    simulate_button_release_with_debounce();

    TEST_ASSERT_FALSE_MESSAGE(pwrkey_pressed(), "Initial state should be released");

    // Симуляция нажатия кнопки
    simulate_button_press();

    pwrkey_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_pressed(), "Button should not register as pressed immediately");

    // Продвигаем время, но недостаточно для окончания подавления дребезга
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS);
    pwrkey_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_pressed(), "Button should not register as pressed before debounce time");

    // Продвигаем время, чтобы завершилось подавление дребезга
    utest_systick_advance_time_ms(1);
    pwrkey_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(pwrkey_pressed(), "Button should register as pressed after debounce time");
}

// Сценарий: Эмуляция кратковременного импульса кнопки (нажатие, затем быстрое отпускание)
// Ожидается: Импульс отфильтрован, состояние кнопки остаётся отпущенной
static void test_pwrkey_debounce_glitch_rejection(void)
{
    LOG_INFO("Testing debounce glitch rejection");

    pwrkey_init();

    // Начинаем с отпущенной кнопки
    simulate_button_release_with_debounce();

    // Симуляция импульса (короткое нажатие)
    simulate_button_press();
    pwrkey_do_periodic_work();

    // Немного продвигаем время
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS / 2);
    pwrkey_do_periodic_work();

    // Помеха завершилась - кнопка снова отпущена
    simulate_button_release_with_debounce();

    TEST_ASSERT_FALSE_MESSAGE(pwrkey_pressed(), "Button should still be released after glitch rejection");
}

// Сценарий: Эмуляция длительной серии импульсов во время попытки нажатия
// Ожидается: Все импульсы отфильтрованы, состояние кнопки регистрируется только после стабильного нажатия
static void test_pwkrey_debounce_long_glith_rejection_press(void)
{
    LOG_INFO("Testing long time glitch rejection during press");

    pwrkey_init();

    // Начинаем с отпущенной кнопки
    simulate_button_release_with_debounce();

    // Эмулируем импульсы (краткие нажатия)
    systime_t glitch_time = 5;
    unsigned time = 0;
    while (time <= (PWRKEY_DEBOUNCE_MS + 1)) {
        simulate_button_press();
        pwrkey_do_periodic_work();
        utest_systick_advance_time_ms(glitch_time);
        pwrkey_do_periodic_work();

        TEST_ASSERT_FALSE_MESSAGE(pwrkey_pressed(), "Button should still be released during glitch rejection");

        simulate_button_release();
        pwrkey_do_periodic_work();
        utest_systick_advance_time_ms(glitch_time);
        pwrkey_do_periodic_work();

        TEST_ASSERT_FALSE_MESSAGE(pwrkey_pressed(), "Button should still be released during glitch rejection");

        time += glitch_time;
    }

    // Теперь нажимаем кнопку постоянно
    simulate_button_press();
    pwrkey_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(pwrkey_pressed(), "Button should still be released just after glitch rejection");

    // Немного продвигаем время
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS / 2);
    pwrkey_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(pwrkey_pressed(), "Button should still be released before debounce time elapsed");

    // Продвигаем время, чтобы достичь конца интервала подавления дребезга
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS / 2 + 2);
    pwrkey_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(pwrkey_pressed(), "Button should be pressed after debounce time elapsed");
}

// Сценарий: Эмуляция длительной серии импульсов во время попытки отпускания
// Ожидается: Все импульсы отфильтрованы, состояние кнопки регистрируется как "отпущенна" только после
// стабильного отпускания
static void test_pwkrey_debounce_long_glith_rejection_release(void)
{
    LOG_INFO("Testing long time glitch rejection during release");

    pwrkey_init();

    // Начинаем с нажатой кнопки
    simulate_button_press_with_debounce();

    // Эмулируем импульсы (краткие нажатия)
    systime_t glitch_time = 5;
    unsigned time = 0;
    while (time <= (PWRKEY_DEBOUNCE_MS + 1)) {
        simulate_button_release();
        pwrkey_do_periodic_work();
        utest_systick_advance_time_ms(glitch_time);
        pwrkey_do_periodic_work();

        TEST_ASSERT_TRUE_MESSAGE(pwrkey_pressed(), "Button should still be pressed during glitch rejection");

        simulate_button_press();
        pwrkey_do_periodic_work();
        utest_systick_advance_time_ms(glitch_time);
        pwrkey_do_periodic_work();

        TEST_ASSERT_TRUE_MESSAGE(pwrkey_pressed(), "Button should still be pressed during glitch rejection");

        time += glitch_time;
    }

    // Теперь отпускаем кнопку постоянно
    simulate_button_release();
    pwrkey_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(pwrkey_pressed(), "Button should still be pressed just after glitch rejection");

    // Немного продвигаем время
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS / 2);
    pwrkey_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(pwrkey_pressed(), "Button should still be pressed before debounce time elapsed");

    // Продвигаем время, чтобы достичь конца интервала подавления дребезга
    utest_systick_advance_time_ms(PWRKEY_DEBOUNCE_MS / 2 + 2);
    pwrkey_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(pwrkey_pressed(), "Button should be released after debounce time elapsed");
}

// Сценарий: Нажатие и быстрое отпускание кнопки (меньше порога длительного нажатия)
// Ожидается: pwrkey_handle_short_press() возвращает true, флаг сбрасывается после чтения
static void test_pwrkey_short_press_detection(void)
{
    LOG_INFO("Testing short press detection");

    pwrkey_init();

    // Начинаем с отпущенной кнопки
    simulate_button_release_with_debounce();

    // Короткое нажатие ещё не обнаружено
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_short_press(), "No short press should be detected initially");

    // Эмулируем нажатие кнопки
    simulate_button_press_with_debounce();

    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_short_press(), "No short press while button is held");

    // Держим короткое время (меньше времени длительного нажатия)
    utest_systick_advance_time_ms(PWRKEY_LONG_PRESS_TIME_MS / 2);
    pwrkey_do_periodic_work();

    // Отпускаем кнопку
    simulate_button_release_with_debounce();

    // Должно быть обнаружено короткое нажатие
    TEST_ASSERT_TRUE_MESSAGE(pwrkey_handle_short_press(), "Short press should be detected after button release");

    // Флаг должен быть сброшен после обработки
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_short_press(), "Short press flag should be cleared after handling");
}

// Сценарий: Удержание кнопки нажатой дольше порога длительного нажатия
// Ожидается: pwrkey_handle_long_press() возвращает true после порога,
// флаг сбрасывается после чтения
static void test_pwrkey_long_press_detection(void)
{
    LOG_INFO("Testing long press detection");

    pwrkey_init();

    // Начинаем с отпущенной кнопки
    simulate_button_release_with_debounce();

    // Длительное нажатие ещё не обнаружено
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_long_press(), "No long press should be detected initially");

    // Эмулируем нажатие кнопки
    simulate_button_press_with_debounce();

    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_long_press(), "No long press immediately after button press");

    // Держим чуть меньше порога длительного нажатия
    utest_systick_advance_time_ms(PWRKEY_LONG_PRESS_TIME_MS - 1);
    pwrkey_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_long_press(), "No long press before threshold time");

    // Держим ещё одну миллисекунду, чтобы точно достичь порога
    utest_systick_advance_time_ms(1);
    pwrkey_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_long_press(), "No long press at exact threshold (needs > not >=)");

    // Держим ещё одну миллисекунду, чтобы превысить порог
    utest_systick_advance_time_ms(1);
    pwrkey_do_periodic_work();

    // Длительное нажатие должно быть обнаружено
    TEST_ASSERT_TRUE_MESSAGE(pwrkey_handle_long_press(), "Long press should be detected after threshold exceeded");

    // Флаг должен быть сброшен после обработки
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_long_press(), "Long press flag should be cleared after handling");
}

// Сценарий: Удержание кнопки в течение времени длительного нажатия, затем отпускание
// Ожидается: Обнаружено только длительное нажатие, событие по короткому нажатию не генерируется
static void test_pwrkey_long_press_no_short_press(void)
{
    LOG_INFO("Testing that long press does not trigger short press");

    pwrkey_init();

    // Начинаем с отпущенной кнопки
    simulate_button_release_with_debounce();

    // Эмулируем нажатие кнопки
    simulate_button_press_with_debounce();

    // Держим в течение времени длительного нажатия
    utest_systick_advance_time_ms(PWRKEY_LONG_PRESS_TIME_MS + 1);
    pwrkey_do_periodic_work();

    // Длительное нажатие должно быть обнаружено
    TEST_ASSERT_TRUE_MESSAGE(pwrkey_handle_long_press(), "Long press should be detected");

    // Отпускаем кнопку
    simulate_button_release_with_debounce();

    // Короткое нажатие НЕ должно быть обнаружено (так как это было длительное нажатие)
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_short_press(), "Short press should NOT be detected after long press");
}

// Сценарий: Выполнение нескольких коротких нажатий кнопки подряд
// Ожидается: Каждое короткое нажатие обнаруживается независимо
static void test_pwrkey_multiple_short_presses(void)
{
    LOG_INFO("Testing multiple short presses");

    pwrkey_init();

    // Начинаем с отпущенной кнопки
    simulate_button_release_with_debounce();

    // Первое короткое нажатие
    simulate_button_press_with_debounce();
    simulate_button_release_with_debounce();

    TEST_ASSERT_TRUE_MESSAGE(pwrkey_handle_short_press(), "First short press should be detected");

    // Второе короткое нажатие
    simulate_button_press_with_debounce();
    simulate_button_release_with_debounce();

    TEST_ASSERT_TRUE_MESSAGE(pwrkey_handle_short_press(), "Second short press should be detected");
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_short_press(), "No more short presses should be pending");
}

// Сценарий: Выполнение нескольких длительных нажатий кнопки подряд
// Ожидается: Каждое длительное нажатие обнаруживается один раз (не повторяется во время удержания),
// событие короткого нажатия не генерируется после отпускания
static void test_pwrkey_multiple_long_presses(void)
{
    LOG_INFO("Testing multiple long presses");

    pwrkey_init();

    // Начинаем с отпущенной кнопки
    simulate_button_release_with_debounce();

    // === Первое длительное нажатие ===
    simulate_button_press_with_debounce();

    // Удерживаем в течение времени длительного нажатия
    utest_systick_advance_time_ms(PWRKEY_LONG_PRESS_TIME_MS + 1);
    pwrkey_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(pwrkey_handle_long_press(), "First long press should be detected");

    // Продолжаем удерживать - длительное нажатие не должно повторяться
    utest_systick_advance_time_ms(PWRKEY_LONG_PRESS_TIME_MS + 1);
    pwrkey_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_long_press(), "Long press should not repeat while button is still held");

    // Отпускаем кнопку
    simulate_button_release_with_debounce();

    // Короткое нажатие не должно генерироваться после длительного нажатия
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_short_press(), "No short press after long press");

    // === Второе длительное нажатие ===
    simulate_button_press_with_debounce();

    // Удерживаем в течение времени длительного нажатия
    utest_systick_advance_time_ms(PWRKEY_LONG_PRESS_TIME_MS + 1);
    pwrkey_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(pwrkey_handle_long_press(), "Second long press should be detected");
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_long_press(), "No more long presses should be pending");
}

// Сценарий: Кнопка уже нажата во время загрузки (например, включение кнопкой)
// Ожидается: События нажатия не генерируются, пока кнопка удерживается с загрузки; события
// генерируются корректно после первого отпускания и повторного нажатия
static void test_pwrkey_pressed_on_boot(void)
{
    LOG_INFO("Testing button held pressed during boot (no events should be generated)");

    pwrkey_init();

    // Эмулируем, что кнопка УЖЕ НАЖАТА при загрузке (например, включение кнопкой и пользователь удерживает её)
    simulate_button_press_with_debounce();

    // Кнопка должна обнаруживаться как нажатая
    TEST_ASSERT_TRUE_MESSAGE(pwrkey_ready(), "pwrkey should be ready after debounce");
    TEST_ASSERT_TRUE_MESSAGE(pwrkey_pressed(), "pwrkey should detect button as pressed");

    // Однако события нажатия НЕ должны генерироваться (ни короткое, ни длительное)
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_short_press(), "No short press should be detected when button held on boot");
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_long_press(), "No long press should be detected when button held on boot");

    // Продолжаем удерживать кнопку в течение времени длительного нажатия
    utest_systick_advance_time_ms(PWRKEY_LONG_PRESS_TIME_MS + 1);
    pwrkey_do_periodic_work();

    // Всё ещё не должно генерироваться событие длительного нажатия
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_long_press(), "No long press should be detected for button held since boot");

    // Теперь отпускаем кнопку
    simulate_button_release_with_debounce();

    // Всё ещё не должно генерироваться короткое нажатие после отпускания
    TEST_ASSERT_FALSE_MESSAGE(pwrkey_handle_short_press(), "No short press after releasing button held since boot");

    // Теперь нажимаем кнопку снова - это должно сгенерировать события нажатия
    simulate_button_press_with_debounce();

    // Держим короткое время и отпускаем
    utest_systick_advance_time_ms(PWRKEY_LONG_PRESS_TIME_MS / 2);
    pwrkey_do_periodic_work();

    simulate_button_release_with_debounce();

    // Теперь короткое нажатие должно обнаруживаться
    TEST_ASSERT_TRUE_MESSAGE(pwrkey_handle_short_press(), "Short press should be detected after proper button press (after boot-held button was released)");
}

int main(void)
{
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        LOG_INFO("Running tests with active LOW power key configuration");
    #else
        LOG_INFO("Running tests with active HIGH power key configuration");
    #endif

    LOG_MESSAGE();

    UNITY_BEGIN();

    RUN_TEST(test_pwrkey_init);
    RUN_TEST(test_pwrkey_ready_after_debounce);
    RUN_TEST(test_pwrkey_pressed_state);
    RUN_TEST(test_pwrkey_debounce_on_press);
    RUN_TEST(test_pwrkey_debounce_glitch_rejection);
    RUN_TEST(test_pwkrey_debounce_long_glith_rejection_press);
    RUN_TEST(test_pwkrey_debounce_long_glith_rejection_release);
    RUN_TEST(test_pwrkey_short_press_detection);
    RUN_TEST(test_pwrkey_long_press_detection);
    RUN_TEST(test_pwrkey_long_press_no_short_press);
    RUN_TEST(test_pwrkey_multiple_short_presses);
    RUN_TEST(test_pwrkey_multiple_long_presses);
    RUN_TEST(test_pwrkey_pressed_on_boot);

    return UNITY_END();
}
