#pragma once
#include <stdbool.h>
#include "wbmcu_system.h"
#include "config.h"

enum mcu_poweron_reason {
    MCU_POWERON_REASON_POWER_ON,
    MCU_POWERON_REASON_POWER_KEY,
    MCU_POWERON_REASON_RTC_ALARM,
    MCU_POWERON_REASON_RTC_PERIODIC_WAKEUP,
    MCU_POWERON_REASON_UNKNOWN,
};

enum mcu_vcc_5v_state {
    MCU_VCC_5V_STATE_OFF = 0,
    MCU_VCC_5V_STATE_ON = 1,
};

void mcu_init_poweron_reason(void);
enum mcu_poweron_reason mcu_get_poweron_reason(void);
void mcu_goto_standby(uint16_t wakeup_after_s);
enum mcu_vcc_5v_state mcu_get_vcc_5v_last_state(void);
void mcu_save_vcc_5v_last_state(enum mcu_vcc_5v_state state);
void mcu_check_vbat_do_periodic_work(void);

// --- suspend-to-off: состояние окна сна в backup-домене RTC/TAMP ---
// Во время окна suspend-to-off EC спит в STM32 Standby (как в poweroff).
// Standby стирает SRAM, поэтому факт "EC спит в окне suspend-to-off" и остаток
// дедлайна живут в backup-регистрах TAMP: они питаются от VBAT и переживают
// Standby-сброс. Регистр 0 занят состоянием 5В (mcu_save_vcc_5v_last_state).

// Взводит метку suspend-to-off и сохраняет остаток дедлайна (в секундах)
// перед входом в Standby.
void mcu_suspend_arm(uint32_t remaining_deadline_s);
// Потребляет метку на пробуждении-сбросе: true ровно один раз, если этот
// сброс - выход из Standby со взведённой меткой. Метка стирается безусловно:
// метка, пережившая обесточивание (её хранит VBAT), не должна быть позже
// ошибочно прочитана как resume на обычном пробуждении из Standby.
bool mcu_suspend_take_resume(void);
// Остаток дедлайна (в секундах), сохранённый mcu_suspend_arm() перед сном.
uint32_t mcu_suspend_get_remaining_s(void);
