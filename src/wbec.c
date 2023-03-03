#include "config.h"
#include "regmap.h"
#include "pwrkey.h"
#include "irq-subsystem.h"
#include "wdt.h"

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
    if (pwrkey_handle_short_press()) {
        irq_set_flag(IRQ_PWR_OFF_REQ);
    }
}

