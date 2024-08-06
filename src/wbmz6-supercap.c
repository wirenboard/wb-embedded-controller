#include "config.h"

#if defined WBEC_WBMZ6_SUPPORT

#include "wbmz6-supercap.h"
#include "adc.h"

bool wbmz6_supercap_is_present(void)
{
    uint16_t supercap_mv = adc_get_ch_mv(ADC_CHANNEL_ADC_VBAT);
    if (supercap_mv > WBEC_WBMZ6_SUPERCAP_VOLTAGE_MIN_MV) {
        return true;
    }
    return false;
}

void wbmz6_supercap_init(void)
{

}

void wbmz6_supercap_update_params(struct wbmz6_params *params)
{
    params->full_design_capacity_mah = WBEC_WBMZ6_SUPERCAP_FULL_DESIGN_CAPACITY_UWH;
    params->voltage_min_mv = WBEC_WBMZ6_SUPERCAP_VOLTAGE_MIN_MV;
    params->voltage_max_mv = WBEC_WBMZ6_SUPERCAP_VOLTAGE_MAX_MV;
    // params->charge_current_ma = WBEC_WBMZ6_SUPERCAP_CHARGE_CURRENT_MA;
}


void wbmz6_supercap_update_status(struct wbmz6_status *status)
{
    status->voltage_now_mv = adc_get_ch_mv(ADC_CHANNEL_ADC_VBAT);
    status->current_now_ma = 0;
    status->capacity_percent = 100;
    status->temperature = 0;
}

#endif
