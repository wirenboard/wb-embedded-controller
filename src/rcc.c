#include "wbmcu_system.h"

uint32_t SystemCoreClock = 16000000;        // Default value after reset

void rcc_set_hsi_1mhz_clock(void)
{
    FLASH->ACR &= ~FLASH_ACR_LATENCY_Msk;
    FLASH->ACR |= FLASH_ACR_LATENCY_0;                      // One wait state

    RCC->CFGR &= ~RCC_CFGR_SW_Msk;                          // Clear SW bits - HSI as System clock
    while ((RCC->CFGR & RCC_CFGR_SWS) != 0) {};             // Wait for system clock switching

    RCC->CR &= ~RCC_CR_HSIDIV;                              // Clear HSI divider bits
    RCC->CR |= 4 << RCC_CR_HSIDIV_Pos;                      // HSI divider = 16

    RCC->CR &= ~RCC_CR_PLLON;                               // Disable PLL

    SystemCoreClock = 1000000;
}

void rcc_set_hsi_pll_64mhz_clock(void)
{
    FLASH->ACR &= ~FLASH_ACR_LATENCY_Msk;
    FLASH->ACR |= FLASH_ACR_LATENCY_1;                      // Two wait states

    RCC->CR &= ~RCC_CR_HSIDIV;                              // Clear HSI divider bits

    RCC->PLLCFGR |= (0b001 << RCC_PLLCFGR_PLLM_Pos);        // Division factor M = 2
    RCC->PLLCFGR |= (16 << RCC_PLLCFGR_PLLN_Pos);           // PLL frequency multiplication factor N = 16
    RCC->PLLCFGR |= RCC_PLLCFGR_PLLSRC_HSI;                 // HSI16 as PLL input clock source
    RCC->PLLCFGR |= (0b001 << RCC_PLLCFGR_PLLR_Pos);        // PLL VCO division factor R for PLLRCLK clock output = 2
    RCC->PLLCFGR |= RCC_PLLCFGR_PLLREN;                     // PLLRCLK clock output enable

    RCC->CR |= RCC_CR_PLLON;                                // Enable PLL
    while ((RCC->CR & RCC_CR_PLLRDY) == 0) {};              // Wait until PLL started

    RCC->CFGR &= ~RCC_CFGR_SW_Msk;                          // Clear SW bits
    RCC->CFGR |= RCC_CFGR_SW_1;                             // PLLRCLK as System clock
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_1) {};// Wait for system clock switching

    SystemCoreClock = 64000000;
}
