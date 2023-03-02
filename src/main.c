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
    bool pwrkey_pressed = false;

    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    RCC->IOPENR |= RCC_IOPENR_GPIOBEN;
    RCC->IOPENR |= RCC_IOPENR_GPIOCEN;

    GPIO_SET_INPUT(PWR_KEY_PORT, PWR_KEY_PIN);
    GPIO_SET_PULLUP(PWR_KEY_PORT, PWR_KEY_PIN);

    GPIO_RESET(INT_PORT, INT_PIN);
    GPIO_SET_OD(INT_PORT, INT_PIN);
    GPIO_SET_OUTPUT(INT_PORT, INT_PIN);

    adc_init();
    i2c_slave_init();
    rtc_init();

    system_led_init();

    struct REGMAP_INFO wbec_info = {
        .wbec_id = WBEC_ID,
        .board_rev = 0x55,
        .fw_ver_major = fw_ver[0],
        .fw_ver_minor = fw_ver[1],
        .fw_ver_patch = fw_ver[2],
        .fw_ver_suffix = fw_ver[3],
    };

    regmap_set_region_data(REGMAP_REGION_INFO, &wbec_info, sizeof(wbec_info));

    while (1) {
        system_led_on();
        delay(300);
        system_led_off();
        delay(300);

        rtc_alarm_do_periodic_work();

        // IRQ to regmap
        irq_flags_t irq_flags = irq_get_flags();
        regmap_set_region_data(REGMAP_REGION_IRQ_FLAGS, &irq_flags, sizeof(irq_flags));

        // Set PWR KEY flag
        if (!pwrkey_pressed && !GPIO_TEST(PWR_KEY_PORT, PWR_KEY_PIN)) {

        }

        // Update IRQ flags
        if (!pwrkey_pressed && !GPIO_TEST(PWR_KEY_PORT, PWR_KEY_PIN)) {
            pwrkey_pressed = 1;
            irq_set_flag(IRQ_PWR_OFF_REQ);
        } else if (pwrkey_pressed && GPIO_TEST(PWR_KEY_PORT, PWR_KEY_PIN)) {
            pwrkey_pressed = 0;
        }

        if (regmap_snapshot_is_region_changed(REGMAP_REGION_IRQ_MSK)) {
            irq_flags_t msk;
            regmap_get_snapshop_region_data(REGMAP_REGION_IRQ_MSK, &msk, sizeof(msk));
            irq_set_mask(msk);
        }

        if (regmap_snapshot_is_region_changed(REGMAP_REGION_IRQ_CLEAR)) {
            irq_flags_t clear;
            regmap_get_snapshop_region_data(REGMAP_REGION_IRQ_CLEAR, &clear, sizeof(clear));
            irq_clear_flags(clear);
        }


        for (enum adc_channel ch = 0; ch < ADC_CHANNEL_COUNT; ch++) {
            // TODO Calc real value
            // regmap_set_adc_ch(ch, fix16_to_int(adc_get_channel_raw_value(ch)));
        }

        // Set INT pin
        if (irq_is_masked_irq()) {
            GPIO_SET(INT_PORT, INT_PIN);
        } else {
            GPIO_RESET(INT_PORT, INT_PIN);
        }
    }
}
