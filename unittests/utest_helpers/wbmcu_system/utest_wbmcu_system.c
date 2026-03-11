#include "utest_wbmcu_system.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <setjmp.h>

// UID_BASE mock
const uint32_t uid_base_mock[3] = {0x12345678, 0x9ABCDEF0, 0x11223344};
PWR_TypeDef pwr_mock = {0};

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

void utest_pwr_reset(void)
{
    pwr_mock.CR1 = 0;
    pwr_mock.CR2 = 0;
    pwr_mock.CR3 = 0;
    pwr_mock.CR4 = 0;
    pwr_mock.SR1 = 0;
    pwr_mock.SR2 = 0;
    pwr_mock.SCR = 0;
    pwr_mock.PUCRA = 0;
    pwr_mock.PDCRA = 0;
    pwr_mock.PUCRB = 0;
    pwr_mock.PDCRB = 0;
    pwr_mock.PUCRC = 0;
    pwr_mock.PDCRC = 0;
    pwr_mock.PUCRD = 0;
    pwr_mock.PDCRD = 0;
}
