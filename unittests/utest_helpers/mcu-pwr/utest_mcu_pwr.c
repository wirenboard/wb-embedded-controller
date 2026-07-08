#include "utest_mcu_pwr.h"

// Внутреннее состояние мока
static struct {
    enum mcu_poweron_reason poweron_reason;
    enum mcu_vcc_5v_state vcc_5v_state;
    bool init_called;
    uint16_t standby_wakeup_time;
    bool suspend_resume_pending;
    bool suspend_armed;
    uint32_t suspend_remaining_s;
} mcu_state = {
    .poweron_reason = MCU_POWERON_REASON_POWER_ON,
    .vcc_5v_state = MCU_VCC_5V_STATE_OFF,
    .init_called = false,
    .standby_wakeup_time = 0,
    .suspend_resume_pending = false,
    .suspend_armed = false,
    .suspend_remaining_s = 0,
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

enum mcu_vcc_5v_state mcu_get_vcc_5v_last_state(void)
{
    return mcu_state.vcc_5v_state;
}

void mcu_save_vcc_5v_last_state(enum mcu_vcc_5v_state state)
{
    mcu_state.vcc_5v_state = state;
}

// --- suspend-to-off: метка и остаток дедлайна в "backup-домене" мока ---

void mcu_suspend_arm(uint32_t remaining_deadline_s)
{
    mcu_state.suspend_armed = true;
    mcu_state.suspend_remaining_s = remaining_deadline_s;
}

bool mcu_suspend_take_resume(void)
{
    bool ret = mcu_state.suspend_resume_pending;
    mcu_state.suspend_resume_pending = false;
    return ret;
}

uint32_t mcu_suspend_get_remaining_s(void)
{
    return mcu_state.suspend_remaining_s;
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

void utest_mcu_set_suspend_resume(bool pending)
{
    mcu_state.suspend_resume_pending = pending;
}

void utest_mcu_set_suspend_remaining_s(uint32_t remaining_s)
{
    mcu_state.suspend_remaining_s = remaining_s;
}

bool utest_mcu_was_suspend_armed(void)
{
    return mcu_state.suspend_armed;
}

uint32_t utest_mcu_get_suspend_remaining_s(void)
{
    return mcu_state.suspend_remaining_s;
}

void utest_mcu_reset(void)
{
    mcu_state.poweron_reason = MCU_POWERON_REASON_POWER_ON;
    mcu_state.vcc_5v_state = MCU_VCC_5V_STATE_OFF;
    mcu_state.init_called = false;
    mcu_state.standby_wakeup_time = 0;
    mcu_state.suspend_resume_pending = false;
    mcu_state.suspend_armed = false;
    mcu_state.suspend_remaining_s = 0;
}
