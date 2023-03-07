#include "config.h"
#include "regmap.h"
#include "pwrkey.h"
#include "irq-subsystem.h"
#include "wdt.h"
#include "wb-power.h"
#include "system-led.h"

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
    if (regmap_snapshot_is_region_changed(REGMAP_REGION_POWER_CTRL)) {
        struct REGMAP_POWER_CTRL p;
        regmap_get_snapshop_region_data(REGMAP_REGION_POWER_CTRL, &p, sizeof(p));
        if (p.off) {
            wb_power_off_and_sleep(0);
        }
        if (p.reboot) {
            wdt_stop();
            system_led_blink(50, 50);
            wb_power_reset();
        }

        regmap_snapshot_clear_changed(REGMAP_REGION_POWER_CTRL);
    }

    // Check watchdog timed out
    if (wdt_handle_timed_out()) {
        system_led_blink(50, 50);
        wb_power_reset();
    }
}

