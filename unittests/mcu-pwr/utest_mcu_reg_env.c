#include "utest_mcu_reg_env.h"
#include "rtc.h"
#include "rcc.h"
#include "systick.h"
#include "adc.h"
#include "voltage-monitor.h"
#include "wdt-stm32.h"
#include <string.h>

// ---- Экземпляры регистров, живущие в ОЗУ теста ----
PWR_TypeDef  _utest_pwr;
RTC_TypeDef  _utest_rtc;
EXTI_TypeDef _utest_exti;
SCB_TypeDef  _utest_scb;

utest_irq_handler_t _utest_nvic_handler[UTEST_IRQn_COUNT];

// ---- Внутреннее состояние окружения ----
static struct {
    bool     nvic_enabled[UTEST_IRQn_COUNT];
    uint32_t nvic_disable_count[UTEST_IRQn_COUNT];
    uint32_t nvic_clear_pending_count[UTEST_IRQn_COUNT];

    uint32_t wfi_count;
    uint32_t wfi_scb_scr;
    uint32_t wfi_pwr_cr1;

    uint32_t rtc_mask_alarm_irq_calls;
    bool     rtc_mask_alarm_irq_wpr_was_unlocked;
    uint32_t rtc_periodic_wakeup_period;

    uint32_t rcc_restore_count;
    uint32_t systick_init_count;
    uint32_t adc_init_count;
    uint32_t vmon_settle_count;
    uint32_t watchdog_reload_count;
} env;

void utest_reg_env_reset(void)
{
    memset(&_utest_pwr, 0, sizeof(_utest_pwr));
    memset(&_utest_rtc, 0, sizeof(_utest_rtc));
    memset(&_utest_exti, 0, sizeof(_utest_exti));
    memset(&_utest_scb, 0, sizeof(_utest_scb));
    memset(_utest_nvic_handler, 0, sizeof(_utest_nvic_handler));
    memset(&env, 0, sizeof(env));
}

// ---- NVIC / интринсики ----
void NVIC_EnableIRQ(IRQn_Type irqn)
{
    if ((unsigned)irqn < UTEST_IRQn_COUNT) {
        env.nvic_enabled[irqn] = true;
    }
}

void NVIC_DisableIRQ(IRQn_Type irqn)
{
    if ((unsigned)irqn < UTEST_IRQn_COUNT) {
        env.nvic_enabled[irqn] = false;
        env.nvic_disable_count[irqn]++;
    }
}

void NVIC_ClearPendingIRQ(IRQn_Type irqn)
{
    if ((unsigned)irqn < UTEST_IRQn_COUNT) {
        env.nvic_clear_pending_count[irqn]++;
    }
}

void utest_stop_dsb(void) {}

void utest_stop_wfi(void)
{
    // На железе WFI блокируется до пробуждения; в тесте снимаем состояние
    // регистров в момент сна (SLEEPDEEP снимается уже в W-фазе) и возвращаемся.
    env.wfi_scb_scr = _utest_scb.SCR;
    env.wfi_pwr_cr1 = _utest_pwr.CR1;
    env.wfi_count++;
}

// ---- Заглушки внешних функций, вызываемых mcu-pwr.c ----

// Помощник rtc.c: моделируем защиту записи RTC_CR через WPR так же, как железо —
// снятие ALRAIE проходит, только если WPR разблокирована ключом 0xCA/0x53.
void rtc_mask_alarm_irq(void)
{
    env.rtc_mask_alarm_irq_calls++;
    _utest_rtc.WPR = 0xCA;
    _utest_rtc.WPR = 0x53;
    env.rtc_mask_alarm_irq_wpr_was_unlocked = true;   // WPR открыта на момент записи
    _utest_rtc.CR &= ~RTC_CR_ALRAIE;
    _utest_rtc.WPR = 0;
}

void rtc_set_periodic_wakeup(uint16_t period_s)
{
    env.rtc_periodic_wakeup_period = period_s;
}

void rtc_save_to_tamper_reg(uint8_t index, uint32_t data) { (void)index; (void)data; }
uint32_t rtc_get_tamper_reg(uint8_t index) { (void)index; return 0; }

void rcc_set_hsi_pll_64mhz_clock(void) { env.rcc_restore_count++; }
void systick_init(void)                { env.systick_init_count++; }
void adc_init(enum adc_clock clock_divider, enum adc_vref vref)
{
    (void)clock_divider;
    (void)vref;
    env.adc_init_count++;
}
void vmon_suspend_rearm_settle(void)   { env.vmon_settle_count++; }
void watchdog_reload(void)             { env.watchdog_reload_count++; }

// ---- Аксессоры для теста ----
utest_irq_handler_t utest_nvic_get_handler(IRQn_Type irqn)
{
    if ((unsigned)irqn < UTEST_IRQn_COUNT) {
        return _utest_nvic_handler[irqn];
    }
    return NULL;
}

bool utest_nvic_is_enabled(IRQn_Type irqn)
{
    return ((unsigned)irqn < UTEST_IRQn_COUNT) ? env.nvic_enabled[irqn] : false;
}

uint32_t utest_nvic_get_disable_count(IRQn_Type irqn)
{
    return ((unsigned)irqn < UTEST_IRQn_COUNT) ? env.nvic_disable_count[irqn] : 0;
}

uint32_t utest_nvic_get_clear_pending_count(IRQn_Type irqn)
{
    return ((unsigned)irqn < UTEST_IRQn_COUNT) ? env.nvic_clear_pending_count[irqn] : 0;
}

uint32_t utest_wfi_count(void)      { return env.wfi_count; }
uint32_t utest_wfi_scb_scr(void)    { return env.wfi_scb_scr; }
uint32_t utest_wfi_pwr_cr1(void)    { return env.wfi_pwr_cr1; }

uint32_t utest_rtc_mask_alarm_irq_calls(void) { return env.rtc_mask_alarm_irq_calls; }
bool utest_rtc_mask_alarm_irq_wpr_was_unlocked(void) { return env.rtc_mask_alarm_irq_wpr_was_unlocked; }
uint32_t utest_rtc_periodic_wakeup_period(void) { return env.rtc_periodic_wakeup_period; }

uint32_t utest_rcc_restore_count(void)     { return env.rcc_restore_count; }
uint32_t utest_systick_init_count(void)    { return env.systick_init_count; }
uint32_t utest_adc_init_count(void)        { return env.adc_init_count; }
uint32_t utest_vmon_settle_count(void)     { return env.vmon_settle_count; }
uint32_t utest_watchdog_reload_count(void) { return env.watchdog_reload_count; }
