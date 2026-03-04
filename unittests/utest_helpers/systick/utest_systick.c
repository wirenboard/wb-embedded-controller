#include "systick.h"
#include "utest_systick.h"

static systime_t current_time_ms = 0;

void utest_systick_set_time_ms(systime_t time_ms)
{
    current_time_ms = time_ms;
}

void utest_systick_advance_time_ms(systime_t delta_ms)
{
    current_time_ms += delta_ms;
}

systime_t systick_get_system_time_ms(void)
{
    return current_time_ms;
}

systime_t systick_get_time_since_timestamp(systime_t timestamp)
{
    return current_time_ms - timestamp;
}

// Заглушки для неиспользуемых функций
void systick_init(void) {}
