#pragma once
#include <stdint.h>


// Линия прерывания от EC в линукс
// Меняет состояние на активное, если в EC есть флаги событий
#define EC_GPIO_INT                     GPIOB, 5
#define EC_GPIO_INT_ACTIVE_HIGH
