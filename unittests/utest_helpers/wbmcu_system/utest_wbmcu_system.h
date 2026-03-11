#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <setjmp.h>
#include "wbmcu_system.h"

// Проверить, был ли вызван NVIC_SystemReset()
bool utest_nvic_was_reset_called(void);

// Сбросить состояние мока
void utest_nvic_reset(void);

// Сбросить mock-регистры PWR
void utest_pwr_reset(void);

// Установить jmp_buf для выхода из бесконечного цикла при вызове NVIC_SystemReset()
// Это позволяет тестировать код с while(1) циклами
void utest_nvic_set_exit_jmp(jmp_buf *jmp);
