#include "unity.h"
#include "wdt.h"
#include "config.h"
#include "systick.h"
#include "regmap-int.h"
#include "utest_systick.h"
#include "utest_regmap.h"

#define LOG_LEVEL LOG_LEVEL_INFO
#include "console_log.h"

void setUp(void)
{
    // Сброс всех состояний моков
    utest_systick_set_time_ms(1000);
    utest_regmap_reset();

    // Сброс флага timed_out
    wdt_handle_timed_out();
}

void tearDown(void)
{
}

// Сценарий: Установка таймаута watchdog с нормальными значениями (10 с, 120 с)
// Ожидается: значения таймаута правильно сохранены в regmap
static void test_wdt_set_timeout_normal(void)
{
    LOG_INFO("Testing wdt_set_timeout with normal values");

    // Устанавливаем нормальное значение таймаута
    wdt_set_timeout(10);
    wdt_do_periodic_work();

    struct REGMAP_WDT w;
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(10, w.timeout, "Timeout should be set to 10 seconds");

    // Устанавливаем другое нормальное значение таймаута
    wdt_set_timeout(120);
    wdt_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(120, w.timeout, "Timeout should be set to 120 seconds");
}

// Сценарий: Установка таймаута watchdog в 0
// Ожидается: таймаут ограничен минимальным значением 1 секунда
static void test_wdt_set_timeout_zero(void)
{
    LOG_INFO("Testing wdt_set_timeout with zero");

    wdt_set_timeout(0);
    wdt_do_periodic_work();

    struct REGMAP_WDT w;
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, w.timeout,
                                     "Timeout 0 should be clamped to 1 second");
}

// Сценарий: Установка таймаута watchdog в максимально разрешенное значение
// Ожидается: таймаут установлен в WBEC_WATCHDOG_MAX_TIMEOUT_S без ограничения
static void test_wdt_set_timeout_max(void)
{
    LOG_INFO("Testing wdt_set_timeout with max value");

    wdt_set_timeout(WBEC_WATCHDOG_MAX_TIMEOUT_S);
    wdt_do_periodic_work();

    struct REGMAP_WDT w;
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(WBEC_WATCHDOG_MAX_TIMEOUT_S, w.timeout,
                                     "Timeout should be set to max value");
}

// Сценарий: Установка таймаута watchdog выше максимально разрешенного значения
// Ожидается: таймаут ограничен значением WBEC_WATCHDOG_MAX_TIMEOUT_S
static void test_wdt_set_timeout_over_max(void)
{
    LOG_INFO("Testing wdt_set_timeout with value over max");

    wdt_set_timeout(WBEC_WATCHDOG_MAX_TIMEOUT_S + 100);
    wdt_do_periodic_work();

    struct REGMAP_WDT w;
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(WBEC_WATCHDOG_MAX_TIMEOUT_S, w.timeout,
                                     "Timeout over max should be clamped to max value");
}

// Сценарий: Запуск watchdog, ожидание меньше таймаута, сброс, затем ожидание снова
// Ожидается: Watchdog не срабатывает после первого периода из-за сброса;
// срабатывает после второго периода от точки сброса
static void test_wdt_start_reset(void)
{
    LOG_INFO("Testing wdt_start_reset");

    // Установка таймаута и запуск watchdog
    wdt_set_timeout(5);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Двигаем время, но недостаточно для срабатывания таймаута
    utest_systick_advance_time_ms(4000);
    wdt_do_periodic_work();

    // Еще не должно быть таймаута
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not timeout before period expires");

    // Сброс watchdog
    wdt_start_reset();

    // Двигаем время еще на 4 секунды - все еще не должно быть таймаута, поскольку мы сбросили watchdog
    utest_systick_advance_time_ms(4000);
    wdt_do_periodic_work();

    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not timeout after reset");

    // Двигаем время до момента после таймаута от момента сброса watchdog
    utest_systick_advance_time_ms(2000);
    wdt_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "Watchdog should timeout after period from reset point");
}

// Сценарий: Запуск watchdog с таймаутом, остановка, затем ожидание после таймаута
// Ожидается: Остановленный watchdog не вызывает срабатывание таймаута
static void test_wdt_stop(void)
{
    LOG_INFO("Testing wdt_stop");

    // Установка таймаута и запуск watchdog
    wdt_set_timeout(2);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Остановка watchdog
    wdt_stop();

    // Двигаем время до момента после таймаута
    utest_systick_advance_time_ms(3000);
    wdt_do_periodic_work();

    // Таймаута быть не должно, так как watchdog остановлен
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Stopped watchdog should not timeout");
}

// Сценарий: Запуск watchdog и ожидание достижения таймаута
// Ожидается: Нет таймаута непосредственно перед периодом, таймаут срабатывает после периода
static void test_wdt_timeout_triggers(void)
{
    LOG_INFO("Testing watchdog timeout triggers");

    // Установка таймаута и запуск watchdog
    wdt_set_timeout(3);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Двигаем время на момент прямо перед таймаутом
    utest_systick_advance_time_ms(2999);
    wdt_do_periodic_work();

    // Таймаута быть не должно
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not timeout before timeout period");

    // Двигаем время, чтобы сработал таймаут
    utest_systick_advance_time_ms(2);
    wdt_do_periodic_work();

    // Таймаут должен сработать
    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "Watchdog should timeout after timeout period");
}

// Сценарий: Срабатывание таймаута watchdog и двойной вызов обработчика
// Ожидается: Первый вызов возвращает true и сбрасывает флаг, второй вызов возвращает false
static void test_wdt_handle_timed_out_clears_flag(void)
{
    LOG_INFO("Testing wdt_handle_timed_out clears flag");

    // Установка таймаута и запуск watchdog
    wdt_set_timeout(1);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Вызываем сработку таймаута
    utest_systick_advance_time_ms(1100);
    wdt_do_periodic_work();

    // Первый вызов должен вернуть true
    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "First call should return true for timed out flag");

    // Второй вызов должен вернуть false (флаг сброшен)
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Second call should return false - flag must be cleared");
}

// Сценарий: Срабатывание таймаута watchdog дважды подряд
// Ожидается: Watchdog автоматически сбрасывается после первого таймаута и срабатывает снова
// после следующего периода
static void test_wdt_timeout_auto_resets(void)
{
    LOG_INFO("Testing watchdog auto-resets after timeout");

    // Устанавливаем таймаут и запускаем watchdog
    wdt_set_timeout(2);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Вызываем первый таймаут
    utest_systick_advance_time_ms(2100);
    wdt_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "First timeout should be triggered");

    // Watchdog должен автоматически сброситься, поэтому продвигаем время снова
    utest_systick_advance_time_ms(2100);
    wdt_do_periodic_work();

    // Должен сработать снова
    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "Second timeout should be triggered after auto-reset");
}

// Сценарий: Изменение таймаута watchdog через regmap с 10с на 5с
// Ожидается: Таймаут обновлён в regmap, watchdog сброшен, новый период таймаута
// применяется
static void test_wdt_regmap_timeout_change(void)
{
    LOG_INFO("Testing watchdog timeout change via regmap");

    // Начинаем с начального таймаута
    wdt_set_timeout(10);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Эмулируем изменение таймаута из regmap
    struct REGMAP_WDT w = {
        .timeout = 5,
        .reset = 0
    };

    // Отмечаем регион как изменённый и записываем данные
    TEST_ASSERT_TRUE_MESSAGE(regmap_set_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to set WDT regmap data");
    utest_regmap_mark_region_changed(REGMAP_REGION_WDT);

    // Вызываем do_periodic_work
    wdt_do_periodic_work();

    // Проверяем, что таймаут был обновлён в regmap
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(5, w.timeout,
                                     "Timeout should be updated to 5 seconds from regmap");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, w.reset,
                                     "Reset flag should be cleared");

    // Проверяем, что watchdog был сброшен (временная метка должна быть обновлена)
    // Продвигаем время меньше нового таймаута
    utest_systick_advance_time_ms(4000);
    wdt_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not timeout before new timeout period");

    // Продвигаем, чтобы вызвать новый таймаут
    utest_systick_advance_time_ms(1100);
    wdt_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "Watchdog should timeout after new timeout period");
}

// Сценарий: Изначально таймаут 10с, после 8с уменьшаем таймаут до 5с через regmap
// Ожидается: Watchdog автоматически сбрасывается, чтобы предотвратить ложное срабатывание (8с > 5с, но не должен
// сработать); новый таймаут 5с применяется от точки сброса
static void test_wdt_regmap_timeout_decrease_prevents_false_trigger(void)
{
    LOG_INFO("Testing watchdog auto-reset when timeout decreased via regmap");

    // Начинаем с большим таймаутом (10 секунд)
    wdt_set_timeout(10);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Значительно продвигаем время (8 секунд), но всё ещё меньше текущего таймаута
    utest_systick_advance_time_ms(8000);
    wdt_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not timeout at 8s with 10s timeout");

    // Теперь изменяем таймаут на меньшее значение (5 секунд) через regmap
    // Без автоматического сброса это вызвало бы ложное срабатывание, так как 8с > 5с
    struct REGMAP_WDT w = {
        .timeout = 5,
        .reset = 0
    };

    TEST_ASSERT_TRUE_MESSAGE(regmap_set_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to set WDT regmap data");
    utest_regmap_mark_region_changed(REGMAP_REGION_WDT);

    // Вызываем do_periodic_work - должен автоматически сбросить watchdog
    wdt_do_periodic_work();

    // Проверяем, что таймаут был обновлён
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(5, w.timeout,
                                     "Timeout should be updated to 5 seconds");

    // Критическая проверка: watchdog не должен сработать,
    // потому что он был автоматически сброшен при изменении таймаута
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not trigger - auto-reset prevents false trigger");

    // Теперь продвигаем время меньше нового периода таймаута (4 секунды)
    utest_systick_advance_time_ms(4000);
    wdt_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not timeout before new timeout period");

    // Продвигаем за новый период таймаута
    utest_systick_advance_time_ms(1100);
    wdt_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "Watchdog should timeout after new timeout period from reset point");
}

// Сценарий: Отправка команды сброса watchdog через regmap после 4с из 5с таймаута
// Ожидается: Флаг сброса очищен, таймер watchdog сбрасывается, ожидает ещё 5с до
// таймаута
static void test_wdt_regmap_reset_command(void)
{
    LOG_INFO("Testing watchdog reset command via regmap");

    // Устанавливаем таймаут и запускаем
    wdt_set_timeout(5);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Продвигаем время
    utest_systick_advance_time_ms(4000);
    wdt_do_periodic_work();

    // Отправляем команду сброса через regmap
    struct REGMAP_WDT w = {
        .timeout = 5,
        .reset = 1
    };

    TEST_ASSERT_TRUE_MESSAGE(regmap_set_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to set WDT regmap data");
    utest_regmap_mark_region_changed(REGMAP_REGION_WDT);

    // Вызываем do_periodic_work (должен сбросить watchdog)
    wdt_do_periodic_work();

    // Проверяем, что флаг сброса очищен в regmap
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, w.reset,
                                     "Reset flag should be cleared after processing");

    // Продвигаем время - watchdog не должен сработать, так как был сброшен
    utest_systick_advance_time_ms(4000);
    wdt_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not timeout - it was just reset via regmap");

    // Продвигаем, чтобы вызвать таймаут от точки сброса
    utest_systick_advance_time_ms(1100);
    wdt_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "Watchdog should timeout after period from reset");
}

// Сценарий: Изменение таймаута с 10с на 3с И установка флага сброса одновременно
// Ожидается: Обе операции применены, таймаут обновлён до 3с, флаг сброса очищен,
// watchdog сброшен; применяется новый период таймаута
static void test_wdt_regmap_timeout_and_reset_simultaneous(void)
{
    LOG_INFO("Testing simultaneous timeout change and reset flag via regmap");

    // Устанавливаем таймаут и запускаем
    wdt_set_timeout(10);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Значительно продвигаем время
    utest_systick_advance_time_ms(7000);
    wdt_do_periodic_work();

    // Теперь отправляем одновременно изменение таймаута И флаг сброса в одной транзакции
    struct REGMAP_WDT w = {
        .timeout = 3,
        .reset = 1
    };

    TEST_ASSERT_TRUE_MESSAGE(regmap_set_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to set WDT regmap data");
    utest_regmap_mark_region_changed(REGMAP_REGION_WDT);

    // Вызываем do_periodic_work (должен изменить таймаут и сбросить watchdog)
    wdt_do_periodic_work();

    // Проверяем, что таймаут был обновлён и флаг сброса очищен
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3, w.timeout,
                                     "Timeout should be updated to 3 seconds");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, w.reset,
                                     "Reset flag should be cleared after processing");

    // Проверяем, что watchdog был сброшен (не должен сработать немедленно)
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not timeout immediately after simultaneous changes");

    // Продвигаем время меньше нового таймаута
    utest_systick_advance_time_ms(2500);
    wdt_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not timeout before new timeout period");

    // Продвигаем за новый период таймаута
    utest_systick_advance_time_ms(600);
    wdt_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "Watchdog should timeout after new timeout period");
}

// Сценарий: Установка таймаута в 0 и значение выше максимума через regmap
// Ожидается: Нулевой таймаут ограничивается до 1с, максимальный таймаут ограничивается
// WBEC_WATCHDOG_MAX_TIMEOUT_S
static void test_wdt_regmap_timeout_bounds_via_regmap(void)
{
    LOG_INFO("Testing watchdog timeout bounds via regmap");

    // Тестируем нулевой таймаут через regmap (должен быть ограничен до 1)
    struct REGMAP_WDT w = {
        .timeout = 0,
        .reset = 0
    };

    TEST_ASSERT_TRUE_MESSAGE(regmap_set_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to set WDT regmap data");
    utest_regmap_mark_region_changed(REGMAP_REGION_WDT);
    wdt_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, w.timeout,
                                     "Zero timeout from regmap should be clamped to 1");

    // Тестируем таймаут выше максимума через regmap (должен быть ограничен до максимума)
    w.timeout = WBEC_WATCHDOG_MAX_TIMEOUT_S + 50;
    w.reset = 0;

    TEST_ASSERT_TRUE_MESSAGE(regmap_set_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to set WDT regmap data");
    utest_regmap_mark_region_changed(REGMAP_REGION_WDT);
    wdt_do_periodic_work();

    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(WBEC_WATCHDOG_MAX_TIMEOUT_S, w.timeout,
                                     "Over-max timeout from regmap should be clamped to max");
}

// Сценарий: Вызов функции периодической работы без отметки региона regmap как изменённого
// Ожидается: Значения regmap остаются неизменными, флаг сброса остаётся 0
static void test_wdt_regmap_no_change(void)
{
    LOG_INFO("Testing watchdog when regmap has no changes");

    // Устанавливаем начальный таймаут
    wdt_set_timeout(10);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Получаем текущее состояние regmap
    struct REGMAP_WDT w_before;
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w_before, sizeof(w_before)),
                             "Failed to get WDT regmap data");

    // Вызываем функцию периодической работы без отметки региона как изменённого
    utest_systick_advance_time_ms(1000);
    wdt_do_periodic_work();

    // Regmap должен остаться неизменным
    struct REGMAP_WDT w_after;
    TEST_ASSERT_TRUE_MESSAGE(utest_regmap_get_region_data(REGMAP_REGION_WDT, &w_after, sizeof(w_after)),
                             "Failed to get WDT regmap data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(w_before.timeout, w_after.timeout,
                                     "Timeout should remain unchanged when regmap not changed");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, w_after.reset,
                                     "Reset flag should be 0");
}

// Сценарий: Сброс watchdog 5 раз до истечения таймаута
// Ожидается: Watchdog не срабатывает при повторяющихся сбросах; срабатывает только
// когда не происходит сброс
static void test_wdt_multiple_resets(void)
{
    LOG_INFO("Testing multiple watchdog resets");

    wdt_set_timeout(3);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Сбрасываем несколько раз до истечения таймаута
    for (int i = 0; i < 5; i++) {
        utest_systick_advance_time_ms(2000);
        wdt_start_reset();
        wdt_do_periodic_work();
        TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                                  "Watchdog should not timeout when reset before period");
    }

    // Теперь вызываем срабатывание по таймауту
    utest_systick_advance_time_ms(3100);
    wdt_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "Watchdog should timeout when not reset");
}

// Сценарий: Установка watchdog на максимальный таймаут и ожидание срабатывания
// Ожидается: Watchdog не срабатывает непосредственно перед максимальным таймаутом;
// срабатывает после истечения максимального таймаута
static void test_wdt_long_timeout(void)
{
    LOG_INFO("Testing watchdog with long timeout period");

    // Используем максимальный таймаут
    wdt_set_timeout(WBEC_WATCHDOG_MAX_TIMEOUT_S);
    wdt_start_reset();
    wdt_do_periodic_work();

    // Продвигаем время до момента непосредственно перед таймаутом
    utest_systick_advance_time_ms(WBEC_WATCHDOG_MAX_TIMEOUT_S * 1000 - 100);
    wdt_do_periodic_work();
    TEST_ASSERT_FALSE_MESSAGE(wdt_handle_timed_out(),
                              "Watchdog should not timeout before max timeout period");

    // Триггерим срабатывание по таймауту
    utest_systick_advance_time_ms(200);
    wdt_do_periodic_work();
    TEST_ASSERT_TRUE_MESSAGE(wdt_handle_timed_out(),
                             "Watchdog should timeout after max timeout period");
}

int main(void)
{
    UNITY_BEGIN();

    // Базовые тесты функциональности
    RUN_TEST(test_wdt_set_timeout_normal);
    RUN_TEST(test_wdt_set_timeout_zero);
    RUN_TEST(test_wdt_set_timeout_max);
    RUN_TEST(test_wdt_set_timeout_over_max);

    // Тесты запуска/остановки
    RUN_TEST(test_wdt_start_reset);
    RUN_TEST(test_wdt_stop);

    // Тесты поведения при таймауте
    RUN_TEST(test_wdt_timeout_triggers);
    RUN_TEST(test_wdt_handle_timed_out_clears_flag);
    RUN_TEST(test_wdt_timeout_auto_resets);

    // Тесты интеграции с regmap
    RUN_TEST(test_wdt_regmap_timeout_change);
    RUN_TEST(test_wdt_regmap_timeout_decrease_prevents_false_trigger);
    RUN_TEST(test_wdt_regmap_reset_command);
    RUN_TEST(test_wdt_regmap_timeout_and_reset_simultaneous);
    RUN_TEST(test_wdt_regmap_timeout_bounds_via_regmap);
    RUN_TEST(test_wdt_regmap_no_change);

    // Тесты граничных случаев
    RUN_TEST(test_wdt_multiple_resets);
    RUN_TEST(test_wdt_long_timeout);

    return UNITY_END();
}
