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
#include "voltage-monitor.h"
#include "linux-power-control.h"
#include "rcc.h"
#include "mcu-pwr.h"

int main(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    RCC->IOPENR |= RCC_IOPENR_GPIOBEN;
    RCC->IOPENR |= RCC_IOPENR_GPIOCEN;
    RCC->IOPENR |= RCC_IOPENR_GPIODEN;

    RCC->APBENR1 |= RCC_APBENR1_PWREN;

    // Первым инициализируется WBEC, т.к. он в начале проверяет причину включения
    // и может заснуть обратно, если решит.
    // Также в wbec_init() инициализируется АЦП и настраивается клок на 64 МГц
    wbec_init();

    // Дальше попадаем, только если хотим включаться

    // Init drivers
    systick_init();
    gpio_init();
    system_led_init();
    spi_slave_init();
    regmap_init();
    rtc_init();
    // rtc_enable_pc13_1hz_clkout();
    usart_init();

    // Init subsystems
    irq_init();
    vmon_init();

    // Init WBEC
    wbec_init();

    // Кнопка питания инициализируется последней, т.к.
    // при настройке её как источника пробуждения может быть
    // установлен флаг WKUP, а он проверяется в wbec_init()
    // Errata 2.2.2
    pwrkey_init();

    while (1) {
        // Drivers
        adc_do_periodic_work();
        system_led_do_periodic_work();
        pwrkey_do_periodic_work();
        wdt_do_periodic_work();
        gpio_do_periodic_work();
        linux_pwr_do_periodic_work();

        // Sybsystems
        rtc_alarm_do_periodic_work();
        irq_do_periodic_work();
        vmon_do_periodic_work();

        // Main algorithm
        wbec_do_periodic_work();
    }
}
