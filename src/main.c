#include <stdint.h>

#include "stm32g0xx.h"
#include "gpio.h"
#include "i2c-slave.h"
#include "rtc.h"
#include "regmap.h"
#include "system_led.h"
#include "adc.h"
#include "irq-subsystem.h"
#include "rtc-alarm-subsystem.h"
#include "wbec.h"
#include "pwrkey.h"


void SystemInit(void)
{

}

static void delay(uint32_t ticks)
{
    while(ticks--) {
        __NOP();
    }
}

int main(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    RCC->IOPENR |= RCC_IOPENR_GPIOBEN;
    RCC->IOPENR |= RCC_IOPENR_GPIOCEN;

    // Init drivers
    pwrkey_init();
    adc_init();
    i2c_slave_init();
    rtc_init();
    system_led_init();

    // Init subsystems
    irq_init();

    // Init WBEC
    wbec_init();

    while (1) {
        system_led_on();
        delay(300);
        system_led_off();
        delay(300);

        pwrkey_do_periodic_work();
        rtc_alarm_do_periodic_work();
        irq_do_periodic_work();

        wbec_do_periodic_work();

        for (enum adc_channel ch = 0; ch < ADC_CHANNEL_COUNT; ch++) {
            // TODO Calc real value
            // regmap_set_adc_ch(ch, fix16_to_int(adc_get_channel_raw_value(ch)));
        }
    }
}
