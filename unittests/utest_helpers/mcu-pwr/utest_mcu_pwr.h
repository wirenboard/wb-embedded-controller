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

// --- suspend-to-off: метка и остаток дедлайна ---
// Задать значение, которое (один раз) вернёт mcu_suspend_take_resume() -
// эмуляция пробуждения-сброса из suspend-to-off Standby.
void utest_mcu_set_suspend_resume(bool pending);
// Задать остаток дедлайна, который вернёт mcu_suspend_get_remaining_s().
void utest_mcu_set_suspend_remaining_s(uint32_t remaining_s);
// Был ли вызван mcu_suspend_arm() (взведение метки перед Standby).
bool utest_mcu_was_suspend_armed(void);
// Остаток дедлайна, сохранённый последним mcu_suspend_arm().
uint32_t utest_mcu_get_suspend_remaining_s(void);

// Сбросить состояние мока
void utest_mcu_reset(void);
