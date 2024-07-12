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
#include "test_subsystem.h"
#include "buzzer.h"

int main(void)
{
    RCC->APBENR1 |= RCC_APBENR1_PWREN;
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    RCC->IOPENR |= RCC_IOPENR_GPIOBEN;
    RCC->IOPENR |= RCC_IOPENR_GPIOCEN;
    RCC->IOPENR |= RCC_IOPENR_GPIODEN;
    system_led_init();

    // При включении начинаем всегда с low power run
    // Смотрим причину включения и решаем что делать
    system_led_enable();
    rcc_set_hsi_1mhz_low_power_run();
    systick_init();
    system_led_disable();

    rtc_init();
    // После подачи питания на ЕС или пробуждения из standby
    // Мы находимся в режиме low power run 1 MHz
    // Также тут может быть питание ниже чем 3.3В (от WBMZ BATSENSE)
    // Инициализируем АЦП на внутренний VREF и частоту 1 MHz
    // Это нужно для того, чтобы измерить напряжение на линии +5В и решить что делать дальше
    // Измерение проиходит в wbec_init()
    adc_init(ADC_CLOCK_NO_DIV, ADC_VREF_INT);

    // Первым инициализируется WBEC, т.к. он в начале проверяет причину включения
    // и может заснуть обратно, если решит.
    wbec_init();

    // Дальше попадаем, только если хотим включаться
    rcc_set_hsi_pll_64mhz_clock();
    systick_init();
    adc_init(ADC_CLOCK_DIV_64, ADC_VREF_INT);

    // Init drivers
    gpio_init();
    spi_slave_init();
    regmap_init();
    usart_init();

    // Init subsystems
    irq_init();
    vmon_init();
    buzzer_init();

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

        // Sybsystems
        rtc_alarm_do_periodic_work();
        irq_do_periodic_work();
        vmon_do_periodic_work();
        test_do_periodic_work();
        buzzer_subsystem_do_periodic_work();

        // Main algorithm
        linux_cpu_pwr_seq_do_periodic_work();
        wbec_do_periodic_work();
    }
}
