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
    struct regmap_irq irq_flags = {};
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

    struct regmap_info wbec_info = {
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

        if (rtc_get_ready_read()) {
            struct rtc_time rtc_time;
            struct rtc_alarm rtc_alarm;
            struct regmap_rtc_time regmap_time;
            struct regmap_rtc_alarm regmap_alarm;
            rtc_get_datetime(&rtc_time);
            rtc_get_alarm(&rtc_alarm);

            regmap_time.seconds = BCD_TO_BIN(rtc_time.seconds);
            regmap_time.minutes = BCD_TO_BIN(rtc_time.minutes);
            regmap_time.hours = BCD_TO_BIN(rtc_time.hours);
            regmap_time.days = BCD_TO_BIN(rtc_time.days);
            regmap_time.months = BCD_TO_BIN(rtc_time.months);
            regmap_time.years = BCD_TO_BIN(rtc_time.years);
            regmap_time.weekdays = rtc_time.weekdays;

            regmap_alarm.en = rtc_alarm.enabled;
            regmap_alarm.flag = rtc_alarm.flag;
            regmap_alarm.seconds = BCD_TO_BIN(rtc_alarm.seconds);
            regmap_alarm.minutes = BCD_TO_BIN(rtc_alarm.minutes);
            regmap_alarm.hours = BCD_TO_BIN(rtc_alarm.hours);
            regmap_alarm.days = BCD_TO_BIN(rtc_alarm.days);

            regmap_set_region_data(REGMAP_REGION_RTC_TIME, &regmap_time, sizeof(regmap_time));
            regmap_set_region_data(REGMAP_REGION_RTC_ALARM, &regmap_alarm, sizeof(regmap_alarm));

            if (!irq_flags.rtc_alarm && regmap_alarm.flag) {
                irq_flags.rtc_alarm = 1;
                regmap_set_region_data(REGMAP_REGION_IRQ_FLAGS, &irq_flags, sizeof(irq_flags));
                regmap_set_region_data(REGMAP_REGION_IRQ_MSK, &irq_flags, sizeof(irq_flags));
            }
        }
        
        // Set PWR KEY flag
        if (!pwrkey_pressed && !GPIO_TEST(PWR_KEY_PORT, PWR_KEY_PIN)) {
            
        }

        // Update IRQ flags
        if (!pwrkey_pressed && !GPIO_TEST(PWR_KEY_PORT, PWR_KEY_PIN)) {
            pwrkey_pressed = 1;
            irq_flags.pwroff_req = 1;
            regmap_set_region_data(REGMAP_REGION_IRQ_FLAGS, &irq_flags, sizeof(irq_flags));
            regmap_set_region_data(REGMAP_REGION_IRQ_MSK, &irq_flags, sizeof(irq_flags));
        } else if (pwrkey_pressed && GPIO_TEST(PWR_KEY_PORT, PWR_KEY_PIN)) {
            pwrkey_pressed = 0;
        }

        if (i2c_slave_is_busy()) {
            if (regmap_is_snapshot_region_changed(REGMAP_REGION_RTC_TIME)) {
                struct rtc_time rtc_time;
                struct regmap_rtc_time regmap_time;

                regmap_get_snapshop_region_data(REGMAP_REGION_RTC_TIME, &regmap_time, sizeof(regmap_time));

                rtc_time.seconds = BIN_TO_BCD(regmap_time.seconds);
                rtc_time.minutes = BIN_TO_BCD(regmap_time.minutes);
                rtc_time.hours = BIN_TO_BCD(regmap_time.hours);
                rtc_time.days = BIN_TO_BCD(regmap_time.days);
                rtc_time.months = BIN_TO_BCD(regmap_time.months);
                rtc_time.years = BIN_TO_BCD(regmap_time.years);
                rtc_time.weekdays = regmap_time.weekdays;

                rtc_set_datetime(&rtc_time);
            }
            if (regmap_is_snapshot_region_changed(REGMAP_REGION_RTC_ALARM)) {
                struct rtc_alarm rtc_alarm;
                struct regmap_rtc_alarm regmap_alarm;

                regmap_get_snapshop_region_data(REGMAP_REGION_RTC_ALARM, &regmap_alarm, sizeof(regmap_alarm));

                rtc_alarm.enabled = regmap_alarm.en;
                rtc_alarm.flag = regmap_alarm.flag;
                rtc_alarm.seconds = BIN_TO_BCD(regmap_alarm.seconds);
                rtc_alarm.minutes = BIN_TO_BCD(regmap_alarm.minutes);
                rtc_alarm.hours = BIN_TO_BCD(regmap_alarm.hours);
                rtc_alarm.days = BIN_TO_BCD(regmap_alarm.days);

                rtc_set_alarm(&rtc_alarm);
            }
            if (regmap_is_snapshot_region_changed(REGMAP_REGION_RTC_CFG)) {

            }
            if (regmap_is_snapshot_region_changed(REGMAP_REGION_IRQ_FLAGS)) {
                struct regmap_irq msk;
                regmap_get_snapshop_region_data(REGMAP_REGION_IRQ_FLAGS, &msk, sizeof(msk));
                if (!msk.pwroff_req && irq_flags.pwroff_req) {
                    irq_flags.pwroff_req = 0;
                }
                if (irq_flags.rtc_alarm) {
                    irq_flags.rtc_alarm = 0;
                    rtc_clear_alarm_flag();
                }
            }

            i2c_slave_set_free();
        }

        for (enum adc_channel ch = 0; ch < ADC_CHANNEL_COUNT; ch++) {
            // TODO Calc real value
            // regmap_set_adc_ch(ch, fix16_to_int(adc_get_channel_raw_value(ch)));
        }

        // Set INT pin
        if (irq_flags.pwroff_req || irq_flags.rtc_alarm) {
            GPIO_SET(INT_PORT, INT_PIN);
        } else {
            GPIO_RESET(INT_PORT, INT_PIN);
        }
    }
}
