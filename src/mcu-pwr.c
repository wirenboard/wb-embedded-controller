#include "mcu-pwr.h"
#include "config.h"
#include "wbmcu_system.h"

enum mcu_poweron_reason mcu_get_poweron_reason(void)
{
    enum mcu_poweron_reason reason;
    if (PWR->SR1 & PWR_SR1_SBF) {
        PWR->SCR = PWR_SCR_CSBF;
        if (PWR->SR1 & (1 << (EC_GPIO_PWRKEY_WKUP_NUM + PWR_SR1_WUF1_Pos))) {
            PWR->SCR = (1 << (EC_GPIO_PWRKEY_WKUP_NUM + PWR_SCR_CWUF1));
            reason = MCU_POWERON_REASON_POWER_KEY;
        } else if (PWR_SR1_WUFI) {
            PWR->SCR = PWR_SR1_WUFI;
            if (RTC->SR & RTC_SR_WUTF) {
                reason = MCU_POWERON_REASON_RTC_PERIODIC_WAKEUP;
            } else {
                reason = MCU_POWERON_REASON_RTC_ALARM;
            }
        } else {
            reason = MCU_POWERON_REASON_UNKNOWN;
        }
    } else {
        reason = MCU_POWERON_REASON_POWER_ON;
    }
    return reason;
}

void mcu_goto_standby(void)
{
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
