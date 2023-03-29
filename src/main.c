#include "wbmcu_system.h"
#include "spi-slave.h"
#include "regmap-int.h"
#include "rtc.h"
#include "system-led.h"
#include "adc.h"
#include "irq-subsystem.h"
#include "rtc-alarm-subsystem.h"
#include "wbec.h"
#include "pwrkey.h"
#include "systick.h"
#include "wdt.h"
#include "gpio-subsystem.h"
#include "usart_tx.h"


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

    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    RCC->IOPENR |= RCC_IOPENR_GPIOBEN;
    RCC->IOPENR |= RCC_IOPENR_GPIOCEN;
    RCC->IOPENR |= RCC_IOPENR_GPIODEN;

    RCC->APBENR1 |= RCC_APBENR1_PWREN;

    // Init drivers
    systick_init();
    gpio_init();
    system_led_init();
    pwrkey_init();
    adc_init();
    spi_slave_init();
    regmap_init();
    rtc_init();
    rtc_enable_pc13_1hz_clkout();
    usart_init();

    // Init subsystems
    irq_init();

    // Init WBEC
    wbec_init();

    system_led_blink(500, 1000);

    while (1) {
        // Drivers
        adc_do_periodic_work();
        system_led_do_periodic_work();
        pwrkey_do_periodic_work();
        wdt_do_periodic_work();
        gpio_do_periodic_work();

        // Sybsystems
        rtc_alarm_do_periodic_work();
        irq_do_periodic_work();

        // Main algorithm
        wbec_do_periodic_work();
    }
}
