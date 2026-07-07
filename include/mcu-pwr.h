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

// --- suspend-to-off: окно сна в режиме STM32 Stop 1 ---
// В отличие от Standby, Stop сохраняет SRAM и защёлки GPIO и просыпается на
// месте (WFI возвращается, сброса НЕТ). Источники пробуждения заводятся через
// EXTI/NVIC, а не WKUP. Подробности — в ec-stop-mode-brief.md.

// Однократно вооружает источники пробуждения окна Stop:
//  - RTC (Alarm A + WUT, прямая линия EXTI 19) -> IMR19 + NVIC(RTC_TAMP_IRQn);
//  - кнопка PWRON (PA0, фронт вниз = нажатие) -> EXTI0 + NVIC(EXTI0_1_IRQn);
// и маскирует EXTI9 (SoC-CS/SPI2), чтобы тёмный SoC не будил EC ложно.
// Идемпотентно; вызывать один раз при входе в окно.
void mcu_stop_window_prepare(void);
// Возвращает прерывания к состоянию до окна сна: снимает EXTI-источники
// пробуждения Stop (кнопка EXTI0, RTC EXTI19) и восстанавливает маску EXTI9,
// наложенную mcu_stop_window_prepare(). Вызывать один раз на выходе окна по
// реальному пробуждению (не на кормящих ре-входах).
void mcu_stop_window_finish(void);
// Кормит IWDG и уходит в STM32 Stop 1 (LPMS=001, SLEEPDEEP, WFI). Перед сном
// сбрасывает только устаревшие WUT/кнопка/CWUF (НИКОГДА не CALRAF — реальный
// будильник обязан разбудить). Блокируется до пробуждения по EXTI (тик WUT,
// будильник или фронт кнопки); на выходе снова кормит IWDG и восстанавливает
// тактирование 64 МГц, systick и АЦП. Возвращается после пробуждения.
void mcu_stop_enter(void);
// Потребляет признак «проснулись по тику WUT» (взводит ISR RTC). true один раз
// на каждый истёкший период WUT — база кормления IWDG и дедлайна.
bool mcu_stop_take_wut_tick(void);
// Потребляет признак «проснулись по фронту кнопки» (взводит ISR EXTI0). true
// один раз, если пробуждение было фронтом PWRON, ещё не прошедшим антидребезг.
bool mcu_stop_take_button_wake(void);
