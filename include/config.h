#pragma once
#include <stdint.h>

#define F_CPU       64000000



// Линия прерывания от EC в линукс
// Меняет состояние на активное, если в EC есть флаги событий
#define EC_GPIO_INT                     GPIOB, 5
#define EC_GPIO_INT_ACTIVE_HIGH

// Светодиод для индикации режима работы EC
// Установлен на плате, снаружи не виден
#define EC_GPIO_LED                     GPIOC, 6
#define EC_GPIO_LED_ACTIVE_HIGH

// Кнопка включения
#define EC_GPIO_PWRKEY                  GPIOA, 0
#define EC_GPIO_PWRKEY_ACTIVE_LOW
#define EC_GPIO_PWRKEY_WKUP_NUM         1
#define PWRKEY_DEBOUNCE_MS              50
#define PWRKEY_LONG_PRESS_TIME_MS       3000
