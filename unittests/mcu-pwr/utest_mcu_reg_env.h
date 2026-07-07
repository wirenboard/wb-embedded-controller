#pragma once
// Тестовое окружение register-stub для РЕАЛЬНОГО src/mcu-pwr.c.
// Даёт доступ к перехваченным NVIC/WFI и к счётчикам заглушек внешних функций.
#include <stdint.h>
#include <stdbool.h>
#include "wbmcu_system.h"

// Полный сброс: обнуляет регистры RTC/EXTI/PWR/SCB, NVIC, счётчики и снимок WFI.
void utest_reg_env_reset(void);

// Возвращает обработчик, зарегистрированный mcu_stop_window_prepare() через
// NVIC_SetHandler (для прямого вызова ISR из теста). NULL, если не задан.
utest_irq_handler_t utest_nvic_get_handler(IRQn_Type irqn);

// Состояние NVIC (взводится NVIC_EnableIRQ/DisableIRQ/ClearPendingIRQ).
bool     utest_nvic_is_enabled(IRQn_Type irqn);
uint32_t utest_nvic_get_disable_count(IRQn_Type irqn);
uint32_t utest_nvic_get_clear_pending_count(IRQn_Type irqn);

// Снимок регистров в момент __WFI() (SLEEPDEEP снимается уже ПОСЛЕ пробуждения,
// поэтому проверять его нужно именно на снимке момента сна).
uint32_t utest_wfi_count(void);
uint32_t utest_wfi_scb_scr(void);       // SCB->SCR на момент WFI
uint32_t utest_wfi_pwr_cr1(void);       // PWR->CR1 на момент WFI

// Сколько раз ISR вызвал WPR-разблокированный помощник rtc_mask_alarm_irq()
// (доказывает, что снятие ALRAIE идёт через WPR-путь, а не голой записью CR).
uint32_t utest_rtc_mask_alarm_irq_calls(void);
// Была ли WPR разблокирована на момент записи CR внутри помощника
// (моделирует защиту записи RTC_CR: голая запись без разблокировки — no-op).
bool utest_rtc_mask_alarm_irq_wpr_was_unlocked(void);

// Последний период, запрошенный rtc_set_periodic_wakeup() (mcu_goto_standby).
uint32_t utest_rtc_periodic_wakeup_period(void);

// Счётчики восстановления после Stop (mcu_stop_enter W-фаза).
uint32_t utest_rcc_restore_count(void);
uint32_t utest_systick_init_count(void);
uint32_t utest_adc_init_count(void);
uint32_t utest_vmon_settle_count(void);
uint32_t utest_watchdog_reload_count(void);
