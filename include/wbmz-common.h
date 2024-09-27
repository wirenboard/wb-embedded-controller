#include <stdbool.h>

void wbmz_init(void);
void wbmz_do_periodic_work(void);

bool wbmz_is_powered_from_wbmz(void);

void wbmz_enable_stepup(void);
void wbmz_disable_stepup(void);
bool wbmz_is_stepup_enabled(void);
void wbmz_set_stepup_force_control(bool force_control, bool en);

bool wbmz_is_charging_enabled(void);
void wbmz_set_charging_force_control(bool force_control, bool en);
