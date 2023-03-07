#include "wb-power.h"
#include "systick.h"
#include "gpio.h"
#include "config.h"

enum wb_power_state {
    WB_OFF,
    WB_ON,
    WB_OFF_WAIT_RESET,
    WB_OFF_WAIT_SLEEP
};

struct wb_power_ctx {
    enum wb_power_state state;
    systime_t timestamp;
    uint16_t delay;
};

static struct wb_power_ctx wb_power_ctx;

static inline void a40_5v_off(void)
{
    GPIO_SET(A40_POWER_OFF_PORT, A40_POWER_OFF_PIN);
    wb_power_ctx.state = WB_OFF;
}

static inline void a40_5v_on(void)
{
    GPIO_RESET(A40_POWER_OFF_PORT, A40_POWER_OFF_PIN);
    wb_power_ctx.state = WB_ON;
}

static inline void wbec_goto_standby(void)
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

void wb_power_init(void)
{
    a40_5v_on();
    GPIO_SET_PUSHPULL(A40_POWER_OFF_PORT, A40_POWER_OFF_PIN);
    GPIO_SET_OUTPUT(A40_POWER_OFF_PORT, A40_POWER_OFF_PIN);

    // Set GPIO pull up in standby mode
    // So in standby linux is off
    PWR->PUCRD |= (1 << A40_POWER_OFF_PIN);
    // Pull up for PWR BUTTON
    PWR->PUCRA |= (1 << PWR_KEY_PIN);

    // Enable internal wakeup line (for RTC)
    PWR->CR3 |= PWR_CR3_EIWUL;

    // Set BUTTON pin as wakeup source
    // TODO change for real GPIO
    PWR->CR3 |= PWR_CR3_EWUP1;
    // Set falling edge as wakeup trigger
    PWR->CR4 |= PWR_CR4_WP1;
}

void wb_power_on(void)
{
    a40_5v_on();
}

void wb_power_reset(void)
{
    a40_5v_off();
    wb_power_ctx.state = WB_OFF_WAIT_RESET;
    wb_power_ctx.timestamp = systick_get_system_time();
}

void wb_power_off_and_sleep(uint16_t off_delay)
{
    if (off_delay == 0) {
        a40_5v_off();
        wbec_goto_standby();
        return;
    }
    wb_power_ctx.state = WB_OFF_WAIT_SLEEP;
    wb_power_ctx.timestamp = systick_get_system_time();
    wb_power_ctx.delay = off_delay;
}

void wb_power_do_periodic_work(void)
{
    switch (wb_power_ctx.state) {
    case WB_OFF_WAIT_RESET:
        if (systick_get_time_since_timestamp(wb_power_ctx.timestamp) > WBEC_POWER_RESET_TIME_MS) {
            a40_5v_on();
            wb_power_ctx.state = WB_ON;
        }
        break;

    case WB_OFF_WAIT_SLEEP:
        if (systick_get_time_since_timestamp(wb_power_ctx.timestamp) > wb_power_ctx.delay) {
            a40_5v_off();
            wbec_goto_standby();
        }
        break;

    default:
        break;
    }
}
