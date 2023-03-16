#include "config.h"
#include "regmap.h"
#include "pwrkey.h"
#include "irq-subsystem.h"
#include "wdt.h"
#include "wb-power.h"
#include "system-led.h"
#include "ntc.h"

static const uint8_t fw_ver[] = { MODBUS_DEVICE_FW_VERSION_NUMBERS };

void wbec_init(void)
{
    struct REGMAP_INFO wbec_info = {
        .wbec_id = WBEC_ID,
        .board_rev = 0x55,
        .fw_ver_major = fw_ver[0],
        .fw_ver_minor = fw_ver[1],
        .fw_ver_patch = fw_ver[2],
        .fw_ver_suffix = fw_ver[3],
    };

    regmap_set_region_data(REGMAP_REGION_INFO, &wbec_info, sizeof(wbec_info));

    wdt_set_timeout(WDEC_WATCHDOG_INITIAL_TIMEOUT_S);
}

void wbec_do_periodic_work(void)
{
    // Power off request from button
    if (pwrkey_handle_short_press()) {
        wdt_stop();
        irq_set_flag(IRQ_PWR_OFF_REQ);
        wb_power_off_and_sleep(WBEC_LINUX_POWER_OFF_DELAY_MS);
        system_led_blink(250, 250);
    }

    // Immediately power off from button
    if (pwrkey_handle_long_press()) {
        wb_power_off_and_sleep(0);
    }

    // Linux is ready to power off
    if (regmap_is_region_changed(REGMAP_REGION_POWER_CTRL)) {
        struct REGMAP_POWER_CTRL p;
        regmap_get_region_data(REGMAP_REGION_POWER_CTRL, &p, sizeof(p));
        if (p.off) {
            wb_power_off_and_sleep(0);
        }
        if (p.reboot) {
            wdt_stop();
            system_led_blink(50, 50);
            wb_power_reset();
        }

        regmap_clear_changed(REGMAP_REGION_POWER_CTRL);
    }

    // Check watchdog timed out
    if (wdt_handle_timed_out()) {
        system_led_blink(50, 50);
        wb_power_reset();
    }

    // Get voltages
    struct REGMAP_ADC_DATA adc;
    adc.v_a1 = adc_get_ch_mv(ADC_CHANNEL_ADC_IN1);
    adc.v_a2 = adc_get_ch_mv(ADC_CHANNEL_ADC_IN2);
    adc.v_a3 = adc_get_ch_mv(ADC_CHANNEL_ADC_IN3);
    adc.v_a4 = adc_get_ch_mv(ADC_CHANNEL_ADC_IN4);
    adc.v_in = adc_get_ch_mv(ADC_CHANNEL_ADC_V_IN);
    adc.v_5_0 = adc_get_ch_mv(ADC_CHANNEL_ADC_5V);
    adc.v_3_3 = adc_get_ch_mv(ADC_CHANNEL_ADC_3V3);
    adc.vbus_console = adc_get_ch_mv(ADC_CHANNEL_ADC_VBUS_DEBUG);
    adc.vbus_network = adc_get_ch_mv(ADC_CHANNEL_ADC_VBUS_NETWORK);

    // Calc NTC temp
    fix16_t ntc_raw = adc_get_ch_mv(ADC_CHANNEL_ADC_NTC);
    // Convert to x10 *C
    adc.temp = fix16_to_int(
        fix16_mul(
            ntc_get_temp(ntc_raw),
            F16(100)
        )
    );

    regmap_set_region_data(REGMAP_REGION_ADC_DATA, &adc, sizeof(adc));
}

