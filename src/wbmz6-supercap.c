#include "config.h"

#if defined WBEC_WBMZ6_SUPPORT

#include "wbmz6-supercap.h"
#include "adc.h"
#include "fix16.h"

/**
 * За основу драйвера взят драйвер edlc-battery драйвер из ядра Linux:
 * https://github.com/wirenboard/linux/blob/dev/v5.10.y/drivers/power/supply/edlc-battery.c
 *
 * EDLC mostly behaves as an ideal capacitor, at least when
 * it comes to the scope of this driver.
 * Energy, capacity percentage (i.e. SoC) and current all can
 * be derived from cell voltage.
 *
 * The energy stored in the capacitor (W) is calculated by equation
 * W = (1/2) C V^2, where C is the capacitance (in Farads), and
 * V is the capacitor voltage.
 *
 * As the maximum design energy (in W*h) is somewhat widely adopted
 * parameter of the battery pack, this driver uses it to calculate
 * current_now and energy_now.
 * The energy stored at the given moment, or W_{now}, can be
 * calculated as follows:
 * W_{now} = (1/2) C V_{now}^2, W_{max} = (1/2) C V_{max}^2
 * therefore, W_{now} = W_{max} (V_{now} / V_{max})^2
 *
 * Capacity percent is basically reported as the ratio of the current available
 * energy and the maximum available energy. Available energy can be slightly
 * smaller then the full stored energy because some energy is unrecoverable
 * due cell voltage becoming too small.
 * So capacity (%) is (W_{now} - W_{min}) / (W_{max} - W_{min}), which become
 * (V - V_{max}) (V + V_{max}) / (V_{max} - V_{min}) / (V_{max} + V_{min})
 *
 * Finally, the charging or discharging current can also be derived from
 * voltage measurement. The charging/discharging current i is proportional
 * to the rate of voltage change, and is given by the equation i = C dV / dt.
 * As before, the capacitance value is calcualted from maximum stored energy
 * and maximum cell voltage.
 * W_{max} = (1/2) C V_{max}^2
 * C = 2 W_{max} / V_{max}^2
 * i = 2 (W_{max} / V_{max}^2)  (dV / dt).
 */

#define SUPERCAP_ENERGY_UWH(mv, mf)         (mf * (mv * mv / 1000) / 2 / 3600)
#define SUPERCAP_CURRENT_LOWPASS_RC         1000

static const uint32_t supercap_energy_max_uwh = SUPERCAP_ENERGY_UWH(WBEC_WBMZ6_SUPERCAP_VOLTAGE_MAX_MV, WBEC_WBMZ6_SUPERCAP_CAPACITY_MF);
static const uint32_t supercap_energy_min_uwh = SUPERCAP_ENERGY_UWH(WBEC_WBMZ6_SUPERCAP_VOLTAGE_MIN_MV, WBEC_WBMZ6_SUPERCAP_CAPACITY_MF);

static fix16_t supercap_prev_voltage_mv = 0;
static fix16_t supercap_dv_lowpass = 0;

static inline uint32_t wbmz6_supercap_get_energy_uwh(uint16_t mv)
{
    uint32_t energy = SUPERCAP_ENERGY_UWH(mv, WBEC_WBMZ6_SUPERCAP_CAPACITY_MF);
    return energy;
}

static inline uint8_t wbmz6_supercap_get_capacity_percent(uint32_t energy_now)
{
    if (energy_now < supercap_energy_min_uwh) {
        return 0;
    }
    if (energy_now > supercap_energy_max_uwh) {
        return 100;
    }
    uint32_t energy_range = supercap_energy_max_uwh - supercap_energy_min_uwh;
    uint32_t energy = energy_now - supercap_energy_min_uwh;
    uint8_t capacity = energy * 100 / energy_range;
    return capacity;
}

static int16_t wbmz6_supercap_get_current_ma(fix16_t mv_now)
{
    static const fix16_t lowpass_k = F16(1.0 / (1.0 + (SUPERCAP_CURRENT_LOWPASS_RC / WBEC_WBMZ6_POLL_PERIOD_MS)));

    fix16_t dv_mv = fix16_sub(mv_now, supercap_prev_voltage_mv);
    supercap_prev_voltage_mv = mv_now;
    supercap_dv_lowpass += fix16_mul(
        lowpass_k,
        fix16_sub(
            dv_mv,
            supercap_dv_lowpass
        )
    );

    fix16_t mv_s = fix16_mul(supercap_dv_lowpass, F16(1000.0 / WBEC_WBMZ6_POLL_PERIOD_MS));
    fix16_t ma = fix16_mul(mv_s, F16(WBEC_WBMZ6_SUPERCAP_CAPACITY_MF / 1000.0));

    if (fix16_abs(ma) < F16(WBEC_WBMZ6_SUPERCAP_CURRENT_ZEROING_MA)) {
        return 0;
    }

    return fix16_to_int(ma);
}

bool wbmz6_supercap_is_present(void)
{
    uint16_t supercap_mv = adc_get_ch_mv(ADC_CHANNEL_ADC_VBAT);
    if (supercap_mv > WBEC_WBMZ6_SUPERCAP_DETECT_VOLTAGE_MV) {
        return true;
    }
    return false;
}

void wbmz6_supercap_init(void)
{
    supercap_prev_voltage_mv = adc_get_ch_mv_f16(ADC_CHANNEL_ADC_VBAT);
    supercap_dv_lowpass = 0;
}

void wbmz6_supercap_update_params(struct wbmz6_params *params)
{
    // return milliwatt-hours to fit in uint16_t
    params->full_design_capacity_mah = supercap_energy_max_uwh / 1000;
    params->voltage_min_mv = WBEC_WBMZ6_SUPERCAP_VOLTAGE_MIN_MV;
    params->voltage_max_mv = WBEC_WBMZ6_SUPERCAP_VOLTAGE_MAX_MV;
    params->charge_current_ma = WBEC_WBMZ6_SUPERCAP_CHARGE_CURRENT_MA;
}


void wbmz6_supercap_update_status(struct wbmz6_status *status)
{
    fix16_t mv_f16 = adc_get_ch_mv_f16(ADC_CHANNEL_ADC_VBAT);
    int16_t mv = fix16_to_int(mv_f16);
    uint32_t energy = wbmz6_supercap_get_energy_uwh(mv);
    int16_t current_ma = wbmz6_supercap_get_current_ma(mv_f16);

    status->voltage_now_mv = mv;
    status->capacity_percent = wbmz6_supercap_get_capacity_percent(energy);

    if (current_ma > 0) {
        status->is_charging = 1;
        status->charging_current_ma = current_ma;
        status->discharging_current_ma = 0;
    } else {
        status->is_charging = 0;
        status->charging_current_ma = 0;
        status->discharging_current_ma = -current_ma;
    }

    status->is_inserted = 1;
    status->temperature = 0;

    if (mv < WBEC_WBMZ6_SUPERCAP_VOLTAGE_MIN_MV) {
        status->is_dead = 1;
    } else {
        status->is_dead = 0;
    }
}

#endif
