#pragma once
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

enum mcu_poweron_reason mcu_get_poweron_reason(void);
void mcu_goto_standby(uint16_t wakeup_after_s);
enum mcu_vcc_5v_state mcu_get_vcc_5v_last_state(void);
void mcu_save_vcc_5v_last_state(enum mcu_vcc_5v_state state);
