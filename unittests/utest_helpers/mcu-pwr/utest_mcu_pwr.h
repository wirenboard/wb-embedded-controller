#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "mcu-pwr.h"

// Установить возвращаемое значение mcu_get_poweron_reason()
void utest_mcu_set_poweron_reason(enum mcu_poweron_reason reason);

// Установить состояние 5V питания
void utest_mcu_set_vcc_5v_state(enum mcu_vcc_5v_state state);

// Проверить, был ли вызван mcu_init_poweron_reason()
bool utest_mcu_was_init_called(void);

// Получить параметр wakeup_after_s последнего вызова mcu_goto_standby()
uint16_t utest_mcu_get_standby_wakeup_time(void);

// Сбросить состояние мока
void utest_mcu_reset(void);
