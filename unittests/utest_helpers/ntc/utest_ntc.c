#include "ntc.h"
#include "utest_ntc.h"

static fix16_t ntc_temperature = 0;

void utest_ntc_set_temperature(fix16_t temp_celsius)
{
    ntc_temperature = temp_celsius;
}

fix16_t ntc_convert_adc_raw_to_temp(fix16_t adc_val)
{
    (void)adc_val;
    return ntc_temperature;
}

fix16_t ntc_kohm_to_temp(fix16_t ntc_kohm)
{
    (void)ntc_kohm;
    return ntc_temperature;
}
