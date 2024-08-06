#include <stdbool.h>

void wbmz_init(void);
void wbmz_do_periodic_work(void);
void wbmz_enable_stepup(void);
void wbmz_disable_stepup(void);
bool wbmz_is_powered_from_wbmz(void);
bool wbmz_is_stepup_enabled(void);
bool wbmz_is_charging_enabled(void);
