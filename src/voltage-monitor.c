#include "voltage-monitor.h"
#include "adc.h"
#include "systick.h"

#define __VMON_CH_DATA(vmon_name, adc_name, ok_min, ok_max, fail_min, fail_max) \
    { ADC_CHANNEL_##adc_name, ok_min, ok_max, fail_min, fail_max },

struct vmon_ch_cfg {
    enum adc_channel adc_ch;
    uint16_t ok_min;
    uint16_t ok_max;
    uint16_t fail_min;
    uint16_t fail_max;
};

static const struct vmon_ch_cfg vmon_ch_cfg[VMON_CHANNEL_COUNT] = {
    VOLTAGE_MONITOR_DESC(__VMON_CH_DATA)
};

static bool vmon_ch_status[VMON_CHANNEL_COUNT] = {};
static bool vmon_initialized = 0;
static systime_t start_timestamp;

static inline void check_voltage(const struct vmon_ch_cfg * cfg, bool * status)
{
    uint16_t mv = adc_get_ch_mv(cfg->adc_ch);

    if (*status) {
        // If current status OK, check FAIL limits
        if ((mv < cfg->fail_min) || (mv > cfg->fail_max)) {
            *status = 0;
        }
    } else {
        // If current status FAIL, check OK limits
        if ((mv >= cfg->ok_min) || (mv <= cfg->ok_max)) {
            *status = 1;
        }
    }
}

void vmon_init(void)
{
    start_timestamp = systick_get_system_time_ms();
}

bool vmon_ready(void)
{
    return vmon_initialized;
}

bool vmon_get_ch_status(enum vmon_channel ch)
{
    return vmon_ch_status[ch];
}

void vmon_do_periodic_work(void)
{
    if ((vmon_initialized) ||
        (systick_get_time_since_timestamp(start_timestamp) > VOLTAGE_MONITOR_START_DELAY_MS))
    {
        for (enum vmon_channel ch = 0; ch < VMON_CHANNEL_COUNT; ch++) {
            check_voltage(&vmon_ch_cfg[ch], &vmon_ch_status[ch]);
        }
        vmon_initialized = 1;
    }
}

