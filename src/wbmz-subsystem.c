#include "config.h"
#include "regmap-int.h"
#include "regmap-structs.h"
#include "wbmz-common.h"

void wbmz_subsystem_do_periodic_work(void)
{
    struct REGMAP_PWR_STATUS p = {};
    p.powered_from_wbmz = wbmz_is_powered_from_wbmz();
    p.wbmz_stepup_enabled = wbmz_is_stepup_enabled();

    regmap_set_region_data(REGMAP_REGION_PWR_STATUS, &p, sizeof(p));
}

