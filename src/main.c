#include <stdint.h>

#include "stm32g0xx.h"
#include "gpio.h"
#include "i2c-slave.h"
#include "rtc.h"
#include "regmap.h"
#include "system-led.h"
#include "adc.h"
#include "irq-subsystem.h"
#include "rtc-alarm-subsystem.h"
#include "wbec.h"
#include "pwrkey.h"
#include "systick.h"
#include "wdt.h"


void SystemInit(void)
{

}

int main(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    RCC->IOPENR |= RCC_IOPENR_GPIOBEN;
    RCC->IOPENR |= RCC_IOPENR_GPIOCEN;

    // Init drivers
    systick_init();
    system_led_init();
    pwrkey_init();
    adc_init();
    i2c_slave_init();
    rtc_init();

    // Init subsystems
    irq_init();

    // Init WBEC
    wbec_init();

    system_led_blink(500, 1000);

    while (1) {
        // Drivers
        system_led_do_periodic_work();
        pwrkey_do_periodic_work();
        wdt_do_periodic_work();

        // Sybsystems
        rtc_alarm_do_periodic_work();
        irq_do_periodic_work();

        // Main algorithm
        wbec_do_periodic_work();

        for (enum adc_channel ch = 0; ch < ADC_CHANNEL_COUNT; ch++) {
            // TODO Calc real value
            // regmap_set_adc_ch(ch, fix16_to_int(adc_get_channel_raw_value(ch)));
        }
    }
}
