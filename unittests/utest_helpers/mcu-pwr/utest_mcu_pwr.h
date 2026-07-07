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

// --- suspend-to-off Stop 1 ---
// Если on = true, каждый mcu_stop_enter() моделирует один истёкший период WUT
// (mcu_stop_take_wut_tick() вернёт true) — так продвигается дедлайн по тикам.
void utest_mcu_stop_set_auto_wut_tick(bool on);
// Взвести одиночный тик WUT (следующий mcu_stop_take_wut_tick() вернёт true).
void utest_mcu_stop_inject_wut_tick(void);
// Смоделировать пробуждение по фронту кнопки (mcu_stop_take_button_wake()).
void utest_mcu_stop_set_button_wake(bool pending);
// Был ли вызван mcu_stop_window_prepare() / _finish().
bool utest_mcu_stop_get_window_prepared(void);
bool utest_mcu_stop_get_window_finished(void);
// Сколько раз EC уходил в Stop (вызовов mcu_stop_enter()).
uint32_t utest_mcu_stop_get_enter_count(void);

// Сбросить состояние мока
void utest_mcu_reset(void);
