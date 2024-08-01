#include "config.h"

#if defined WBEC_WBMZ6_SUPPORT


enum wbmz6_device {
    WBMZ6_DEVICE_NONE,
    WBMZ6_DEVICE_BATTERY,
    WBMZ6_DEVICE_SUPERCAP,
};

struct wbmz6_ctx {
    enum wbmz6_device device;
};

// static void wmbz_detect_device(void)
// {
//     if (axp221s_is_present()) {
//         wbmz_ctx.device = WBMZ_DEVICE_BATTERY;
//     } else {
//         uint16_t supercap_mv = adc_get_ch_mv(ADC_CHANNEL_ADC_VBAT);
//         if (supercap_mv > WBEC_WBMZ6_SUPERCAP_VOLTAGE_MIN_MV) {
//             wbmz_ctx.device = WBMZ_DEVICE_SUPERCAP;
//         } else {
//             wbmz_ctx.device = WBMZ_DEVICE_NONE;
//         }
//     }
// }

#endif
