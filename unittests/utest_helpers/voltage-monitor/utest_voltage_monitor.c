#include "voltage-monitor.h"
#include "utest_voltage_monitor.h"

static bool vmon_channel_status[VMON_CHANNEL_COUNT] = {false};
static bool vmon_is_ready = false;

void utest_vmon_set_ch_status(enum vmon_channel ch, bool status)
{
    if (ch < VMON_CHANNEL_COUNT) {
        vmon_channel_status[ch] = status;
    }
}

void utest_vmon_set_ready(bool ready)
{
    vmon_is_ready = ready;
}

void vmon_init(void)
{
    vmon_is_ready = false;
    for (int i = 0; i < VMON_CHANNEL_COUNT; i++) {
        vmon_channel_status[i] = false;
    }
}

bool vmon_ready(void)
{
    return vmon_is_ready;
}

bool vmon_get_ch_status(enum vmon_channel ch)
{
    if (ch < VMON_CHANNEL_COUNT) {
        return vmon_channel_status[ch];
    }
    return false;
}

bool vmon_check_ch_once(enum vmon_channel ch)
{
    return vmon_get_ch_status(ch);
}

void vmon_do_periodic_work(void)
{
}
