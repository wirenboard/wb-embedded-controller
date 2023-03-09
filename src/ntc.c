#include "config.h"

#include "ntc.h"
#include "array_size.h"

#define ADC_MAX_VAL             4095

struct table_info {
    fix16_t out_min;    // Output value corresponding to first table value
    fix16_t out_max;    // Output value corresponding to last table value
};

static const struct table_info table_info_ntc = {
    .out_min = F16(-55),
    .out_max = F16(150)
};

#if (NTC_RES_KOHM == 10)

// Table for 10k B = 3500 (CMFB103J3500HANT)
static const fix16_t table_ntc[] = {
    F16(740.654), F16(517.001), F16(366.615), F16(263.835), F16(192.510),
    F16(142.300), F16(106.474), F16(80.586), F16(61.654), F16(47.652),
    F16(37.186), F16(29.283), F16(23.258), F16(18.624), F16(15.029),
    F16(12.217), F16(10.000), F16(8.240), F16(6.832), F16(5.699),
    F16(4.781), F16(4.033), F16(3.419), F16(2.913), F16(2.494),
    F16(2.145), F16(1.853), F16(1.607), F16(1.399), F16(1.223),
    F16(1.073), F16(0.945), F16(0.835), F16(0.740), F16(0.657),
    F16(0.586), F16(0.524), F16(0.470), F16(0.423), F16(0.381),
    F16(0.344), F16(0.312)
};

#elif (NTC_RES_KOHM == 100)

// Table for 100k B = 3950 (CMFB104J3950HANT)
static const fix16_t table_ntc[] = {
    F16(12882.338), F16(8586.128), F16(5825.362), F16(4018.597), F16(2815.768),
    F16(2002.039), F16(1443.169), F16(1053.847), F16(778.981), F16(582.457),
    F16(440.260), F16(336.206), F16(259.246), F16(201.746), F16(158.371),
    F16(125.353), F16(100.000), F16(80.371), F16(65.055), F16(53.015),
    F16(43.481), F16(35.882), F16(29.784), F16(24.862), F16(20.864),
    F16(17.598), F16(14.917), F16(12.703), F16(10.867), F16(9.336),
    F16(8.054), F16(6.975), F16(6.064), F16(5.291), F16(4.633),
    F16(4.071), F16(3.588), F16(3.173), F16(2.814), F16(2.503),
    F16(2.233), F16(1.997)
};

#else
    #error Not supported NTC
#endif


static fix16_t ntc_get_res_kohm(fix16_t adc_val)
{
    // Vntc = VREF * (Rntc / (Rntc + Rpullup))
    // Vntc = ADC_VAL / ADC_MAX_VAL * VREF
    // ...
    //           ADC_VAL * Rpullup
    // Rntc = -----------------------
    //         ADC_MAX_VAL - ADC_VAL
    //
    // To prevent overflow, use division first, then multiplication

    const fix16_t adc_max_val = F16(ADC_MAX_VAL);
    const fix16_t pullup_res = F16(NTC_PULLUP_RES_KOHM);

    fix16_t r_ntc_kom = 0;

    if (adc_val < adc_max_val) {
        r_ntc_kom = fix16_mul(
            fix16_div(
                adc_val,
                fix16_sub(adc_max_val, adc_val)
            ),
            pullup_res
        );
    }

    return r_ntc_kom;
}

static fix16_t ntc_kohm_to_temp(fix16_t ntc_kohm)
{
    const uint32_t table_size = ARRAY_SIZE(table_ntc);

    if (ntc_kohm > table_ntc[0]) {
        return table_info_ntc.out_min;
    } else if (ntc_kohm < table_ntc[table_size - 1]) {
        return table_info_ntc.out_max;
    }

    uint16_t i_l = 0;
    uint16_t i_h = table_size - 1;
    uint16_t i = (i_l + i_h) / 2;

    while ((i != i_l) && (i != i_h)) {
        if (ntc_kohm < table_ntc[i]) {
            i_l = i;
        } else {
            i_h = i;
        }
        i = (i_l + i_h) / 2;
    }

    fix16_t step = fix16_div(
        fix16_sub(table_info_ntc.out_max, table_info_ntc.out_min),
        fix16_from_int(table_size - 1)
    );
    fix16_t fx0 = fix16_add(
        table_info_ntc.out_min,
        fix16_mul(step, fix16_from_int(i_l))
    );

    fix16_t temp = fix16_add(
        fx0,
        fix16_mul(
            step,
            fix16_div(
                fix16_sub(table_ntc[i_l], ntc_kohm),
                fix16_sub(table_ntc[i_l], table_ntc[i_l + 1])
            )
        )
    );
    return temp;
}

fix16_t ntc_get_temp(fix16_t adc_val)
{
    fix16_t kohm = ntc_get_res_kohm(adc_val);

    return ntc_kohm_to_temp(kohm);
}
