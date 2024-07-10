#pragma once
#include "config.h"

#if defined EC_GPIO_BUZZER

void buzzer_init(void);
void buzzer_beep(uint16_t freq, uint16_t duration_ms);
void buzzer_subsystem_do_periodic_work(void);

#else

static inline void buzzer_init(void) {}
static inline void buzzer_beep(uint16_t freq, uint16_t duration_ms) {}
static inline void buzzer_subsystem_do_periodic_work(void) {}

#endif
