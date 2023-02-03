#include <stdint.h>

#include "stm32g0xx.h"
#include "gpio.h"
#include "i2c-slave.h"
#include "rtc.h"
#include "regmap.h"
#include "system_led.h"
#include "adc.h"


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

    GPIO_SET_INPUT(PWR_KEY_PORT, PWR_KEY_PIN);
    GPIO_SET_PULLUP(PWR_KEY_PORT, PWR_KEY_PIN);
        
    adc_init();
    i2c_slave_init();
    rtc_init();

    system_led_init();
    while (1) {
        system_led_on();
        delay(3000);
        system_led_off();
        delay(3000);

        if (rtc_get_ready_read()) {
            struct rtc_time rtc;
            rtc_get_datetime(&rtc);
            regmap_set_rtc_time(&rtc);
        }

        for (enum adc_channel ch = 0; ch < ADC_CHANNEL_COUNT; ch++) {
            // TODO Calc real value
            regmap_set_adc_ch(ch, fix16_to_int(adc_get_channel_raw_value(ch)));
        }

        if (regmap_is_write_completed()) {
            if (regmap_is_rtc_changed()) {
                struct rtc_time rtc;
                regmap_get_snapshop_rtc_time(&rtc);
                rtc_set_datetime(&rtc);

                regmap_set_rtc_time(&rtc);
            }
            i2c_slave_set_busy(0);
        }
        
        if (GPIO_TEST(PWR_KEY_PORT, PWR_KEY_PIN)) {
            regmap_set_iqr(0x00);
        } else {
            regmap_set_iqr(0x01);
        }
    }
}
