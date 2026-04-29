#include "mcu-pwr.h"
#include "config.h"
#include "wbmcu_system.h"
#include "rtc.h"

static enum mcu_poweron_reason mcu_poweron_reason = MCU_POWERON_REASON_UNKNOWN;

// Вызывать один раз в начале main
void mcu_init_poweron_reason(void)
{
    if (PWR->SR1 & PWR_SR1_SBF) {
        PWR->SCR = PWR_SCR_CSBF;
        if (PWR->SR1 & (1 << (EC_GPIO_PWRKEY_WKUP_NUM - 1 + PWR_SR1_WUF1_Pos))) {
            PWR->SCR = (1 << (EC_GPIO_PWRKEY_WKUP_NUM - 1 + PWR_SCR_CWUF1_Pos));
            mcu_poweron_reason = MCU_POWERON_REASON_POWER_KEY;
        } else if (PWR->SR1 & PWR_SR1_WUFI) {
            if (RTC->SR & RTC_SR_WUTF) {
                RTC->SCR = RTC_SCR_CWUTF;
                mcu_poweron_reason = MCU_POWERON_REASON_RTC_PERIODIC_WAKEUP;
            } else {
                mcu_poweron_reason = MCU_POWERON_REASON_RTC_ALARM;
            }
        }
    } else {
        mcu_poweron_reason = MCU_POWERON_REASON_POWER_ON;
    }
}

enum mcu_poweron_reason mcu_get_poweron_reason(void)
{
    return mcu_poweron_reason;
}

void mcu_goto_standby(uint16_t wakeup_after_s)
{
    if (wakeup_after_s < 1) {
        wakeup_after_s = 1;
    }
    rtc_set_periodic_wakeup(wakeup_after_s);

    // Подробнее про особенности перехода в standby тут:
    // https://community.st.com/t5/stm32-mcus-embedded-software/how-to-enter-standby-or-shutdown-mode-on-stm32/td-p/145849

    // Clear WKUP flags
    // PWR->SCR = PWR_SCR_CWUF;
    // странная история, порядок бит в даташите и в заголовочнике не совпадает
    // используем даташит
    PWR->SCR = 0x003F;

    // SLEEPDEEP
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;

    // 011: Standby mode
    PWR->CR1 |= PWR_CR1_LPMS_0 | PWR_CR1_LPMS_1;
    // Ensure that the previous PWR register operations have been completed
    (void)PWR->CR1;

    while (1) {
        __DSB();
        __WFI();
    };
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
