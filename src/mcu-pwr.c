#include "mcu-pwr.h"
#include "config.h"
#include "wbmcu_system.h"
#include "rtc.h"

enum mcu_poweron_reason mcu_get_poweron_reason(void)
{
    if (PWR->SR1 & PWR_SR1_SBF) {
        PWR->SCR = PWR_SCR_CSBF;
        if (PWR->SR1 & (1 << (EC_GPIO_PWRKEY_WKUP_NUM + PWR_SR1_WUF1_Pos))) {
            PWR->SCR = (1 << (EC_GPIO_PWRKEY_WKUP_NUM + PWR_SCR_CWUF1));
            return MCU_POWERON_REASON_POWER_KEY;
        } else if (PWR_SR1_WUFI) {
            PWR->SCR = PWR_SR1_WUFI;
            if (RTC->SR & RTC_SR_WUTF) {
                return MCU_POWERON_REASON_RTC_PERIODIC_WAKEUP;
            } else {
                return MCU_POWERON_REASON_RTC_ALARM;
            }
        }
    } else {
        return MCU_POWERON_REASON_POWER_ON;
    }
    return MCU_POWERON_REASON_UNKNOWN;
}

void mcu_goto_standby(uint16_t wakeup_after_s)
{
    if (wakeup_after_s < 1) {
        wakeup_after_s = 1;
    }
    rtc_set_periodic_wakeup(wakeup_after_s);

    // Apply pull-up and pull-down configuration
    PWR->CR3 |= PWR_CR3_APC;

    // Clear WKUP flags
    PWR->SCR = PWR_SCR_CWUF;

    // SLEEPDEEP
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;

    // 011: Standby mode
    PWR->CR1 |= PWR_CR1_LPMS_0 | PWR_CR1_LPMS_1;

    __WFI();
    while (1) {};
}

enum mcu_vcc_5v_state mcu_get_vcc_5v_last_state(void)
{
    if (rtc_get_tamper_reg(0) == MCU_VCC_5V_STATE_OFF) {
        return MCU_VCC_5V_STATE_OFF;
    }
    return MCU_VCC_5V_STATE_ON;
}

void mcu_save_vcc_5v_last_state(enum mcu_vcc_5v_state state)
{
    rtc_save_to_tamper_reg(0, state);
}
