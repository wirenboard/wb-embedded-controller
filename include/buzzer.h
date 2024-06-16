#pragma once

#include <stdint.h>

void buzzer_init(void);
void buzzer_start(uint16_t frequency, uint16_t duration, uint8_t volume);
void buzzer_stop();
void buzzer_do_periodic_work(void);