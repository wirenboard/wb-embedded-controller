#include "utest_mcu_pwr.h"

// Внутреннее состояние мока
static struct {
    enum mcu_poweron_reason poweron_reason;
    enum mcu_vcc_5v_state vcc_5v_state;
    bool init_called;
    uint16_t standby_wakeup_time;

    // --- suspend-to-off Stop 1 ---
    bool stop_window_prepared;
    bool stop_window_finished;
    uint32_t stop_enter_count;
    bool stop_auto_wut_tick;        // каждый mcu_stop_enter даёт тик WUT
    bool stop_wut_tick_pending;
    bool stop_button_wake_pending;
} mcu_state = {
    .poweron_reason = MCU_POWERON_REASON_POWER_ON,
    .vcc_5v_state = MCU_VCC_5V_STATE_OFF,
    .init_called = false,
    .standby_wakeup_time = 0,
};

// Реализация функций из mcu-pwr.h
void mcu_init_poweron_reason(void)
{
    mcu_state.init_called = true;
}

enum mcu_poweron_reason mcu_get_poweron_reason(void)
{
    return mcu_state.poweron_reason;
}

void mcu_goto_standby(uint16_t wakeup_after_s)
{
    mcu_state.standby_wakeup_time = wakeup_after_s;
}

void mcu_stop_window_prepare(void)
{
    mcu_state.stop_window_prepared = true;
}

void mcu_stop_window_finish(void)
{
    mcu_state.stop_window_finished = true;
}

void mcu_stop_enter(void)
{
    // На железе блокируется в WFI до пробуждения; в тесте возвращаемся сразу.
    // Тик WUT НЕ взводим по умолчанию (иначе дедлайн срабатывал бы каждый
    // проход); при stop_auto_wut_tick моделируем один истёкший период WUT.
    mcu_state.stop_enter_count++;
    if (mcu_state.stop_auto_wut_tick) {
        mcu_state.stop_wut_tick_pending = true;
    }
}

bool mcu_stop_take_wut_tick(void)
{
    bool ret = mcu_state.stop_wut_tick_pending;
    mcu_state.stop_wut_tick_pending = false;
    return ret;
}

bool mcu_stop_take_button_wake(void)
{
    bool ret = mcu_state.stop_button_wake_pending;
    mcu_state.stop_button_wake_pending = false;
    return ret;
}

enum mcu_vcc_5v_state mcu_get_vcc_5v_last_state(void)
{
    return mcu_state.vcc_5v_state;
}

void mcu_save_vcc_5v_last_state(enum mcu_vcc_5v_state state)
{
    mcu_state.vcc_5v_state = state;
}

// Функции для тестирования
void utest_mcu_set_poweron_reason(enum mcu_poweron_reason reason)
{
    mcu_state.poweron_reason = reason;
}

void utest_mcu_set_vcc_5v_state(enum mcu_vcc_5v_state state)
{
    mcu_state.vcc_5v_state = state;
}

bool utest_mcu_was_init_called(void)
{
    return mcu_state.init_called;
}

uint16_t utest_mcu_get_standby_wakeup_time(void)
{
    return mcu_state.standby_wakeup_time;
}

void utest_mcu_stop_set_auto_wut_tick(bool on)
{
    mcu_state.stop_auto_wut_tick = on;
}

void utest_mcu_stop_inject_wut_tick(void)
{
    mcu_state.stop_wut_tick_pending = true;
}

void utest_mcu_stop_set_button_wake(bool pending)
{
    mcu_state.stop_button_wake_pending = pending;
}

bool utest_mcu_stop_get_window_prepared(void)
{
    return mcu_state.stop_window_prepared;
}

bool utest_mcu_stop_get_window_finished(void)
{
    return mcu_state.stop_window_finished;
}

uint32_t utest_mcu_stop_get_enter_count(void)
{
    return mcu_state.stop_enter_count;
}

void utest_mcu_reset(void)
{
    mcu_state.poweron_reason = MCU_POWERON_REASON_POWER_ON;
    mcu_state.vcc_5v_state = MCU_VCC_5V_STATE_OFF;
    mcu_state.init_called = false;
    mcu_state.standby_wakeup_time = 0;
    mcu_state.stop_window_prepared = false;
    mcu_state.stop_window_finished = false;
    mcu_state.stop_enter_count = 0;
    mcu_state.stop_auto_wut_tick = false;
    mcu_state.stop_wut_tick_pending = false;
    mcu_state.stop_button_wake_pending = false;
}
