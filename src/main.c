#include "wbmcu_system.h"


static inline void rcc_set_hsi_pll_64mhz_clock(void)
{
    FLASH->ACR |= FLASH_ACR_LATENCY_1;                      // Two wait states

    RCC->PLLCFGR |= (0b001 << RCC_PLLCFGR_PLLM_Pos);        // Division factor M = 2
    RCC->PLLCFGR |= (16 << RCC_PLLCFGR_PLLN_Pos);           // PLL frequency multiplication factor N = 16
    RCC->PLLCFGR |= RCC_PLLCFGR_PLLSRC_HSI;                 // HSI16 as PLL input clock source
    RCC->PLLCFGR |= (0b001 << RCC_PLLCFGR_PLLR_Pos);        // PLL VCO division factor R for PLLRCLK clock output = 2
    RCC->PLLCFGR |= RCC_PLLCFGR_PLLREN;                     // PLLRCLK clock output enable

    RCC->CR |= RCC_CR_PLLON;                                // Enable PLL
    while ((RCC->CR & RCC_CR_PLLRDY) == 0) {};              // Wait until PLL started

    RCC->CFGR |= RCC_CFGR_SW_1;                             // PLLRCLK as System clock
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_1) {};// Wait for system clock switching
}

int main(void)
{
    rcc_set_hsi_pll_64mhz_clock();

    while (1) {

    }
}
