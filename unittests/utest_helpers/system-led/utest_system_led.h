#pragma once
#include <stdint.h>
#include <stdbool.h>

// Режимы работы светодиода для тестирования
enum utest_led_mode {
    UTEST_LED_MODE_OFF,
    UTEST_LED_MODE_ON,
    UTEST_LED_MODE_BLINK,
};

// Получить текущий режим работы светодиода
enum utest_led_mode utest_system_led_get_mode(void);

// Получить параметры мигания (on_ms, off_ms)
void utest_system_led_get_blink_params(uint16_t *on_ms, uint16_t *off_ms);

// Получить текущее состояние светодиода (0 - выключен, 1 - включен)
uint8_t utest_system_led_get_state(void);

// Получить количество вызовов system_led_do_periodic_work
uint32_t utest_system_led_get_periodic_work_count(void);

// Сбросить состояние мока
void utest_system_led_reset(void);
