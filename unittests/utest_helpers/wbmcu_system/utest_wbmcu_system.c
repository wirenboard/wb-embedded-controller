#include "utest_wbmcu_system.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>

// UID_BASE mock
const uint32_t uid_base_mock[3] = {0x12345678, 0x9ABCDEF0, 0x11223344};

// Внутреннее состояние мока для NVIC
static struct {
    bool reset_called;
    jmp_buf *exit_jmp;
} nvic_state = {0};

void NVIC_SystemReset(void)
{
    nvic_state.reset_called = true;

    // Если установлен exit_jmp, используем longjmp для выхода из бесконечного цикла
    if (nvic_state.exit_jmp) {
        longjmp(*nvic_state.exit_jmp, 1);
    }
}

// Функции для тестирования
bool utest_nvic_was_reset_called(void)
{
    return nvic_state.reset_called;
}

void utest_nvic_reset(void)
{
    nvic_state.reset_called = false;
    nvic_state.exit_jmp = NULL;
}

void utest_nvic_set_exit_jmp(jmp_buf *jmp)
{
    nvic_state.exit_jmp = jmp;
}


PWR_TypeDef _PWR_instance = {0};

void utest_pwr_reset(void)
{
    memset(&_PWR_instance, 0, sizeof(_PWR_instance));
}
