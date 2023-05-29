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

enum mcu_poweron_reason mcu_get_poweron_reason(void);
void mcu_goto_standby(void);
