#pragma once

// Mock для wdt-stm32.h
// Определяет только необходимые функции без регистров

void watchdog_init(void);
void watchdog_reload(void);
