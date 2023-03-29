#pragma once
#include <stdint.h>

typedef uint32_t systime_t;

void systick_init(void);
systime_t systick_get_system_time(void);
systime_t systick_get_time_since_timestamp(systime_t timestamp);
