#include <stdint.h>
#include <stdbool.h>
#include "hwrev.h"

// Переменные-моки для отслеживания вызовов функций в сценарии несоответствия hwrev
static bool rcc_set_hsi_pll_64mhz_clock_called = false;
static bool spi_slave_init_called = false;

void rcc_set_hsi_pll_64mhz_clock(void)
{
    rcc_set_hsi_pll_64mhz_clock_called = true;
}

void spi_slave_init(void)
{
    spi_slave_init_called = true;
}

// Вспомогательные функции для тестов
bool utest_rcc_set_hsi_pll_64mhz_clock_called(void)
{
    return rcc_set_hsi_pll_64mhz_clock_called;
}

bool utest_spi_slave_was_init_called(void)
{
    return spi_slave_init_called;
}

void utest_hwrev_stubs_reset(void)
{
    rcc_set_hsi_pll_64mhz_clock_called = false;
    spi_slave_init_called = false;
}

