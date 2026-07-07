#include "mcu-pwr.h"
#include "config.h"
#include "wbmcu_system.h"
#include "rtc.h"
#include "rcc.h"
#include "systick.h"
#include "adc.h"
#include "voltage-monitor.h"
#include "wdt-stm32.h"

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

// --- suspend-to-off: окно сна в режиме STM32 Stop 1 ---

// Причины пробуждения из Stop, взводимые ISR и потребляемые главным циклом.
// В Stop, в отличие от Standby, ISR РЕАЛЬНО выполняется при выходе из WFI,
// поэтому обработчики должны снимать линию EXTI, НЕ разрушая защёлку ALRAF,
// которую опрашивает rtc_alarm_do_periodic_work.
static volatile bool stop_wut_tick_pending;
static volatile bool stop_button_wake_pending;

// EXTI-линия 19 (RTC) — ПРЯМАЯ: в RPR1/FPR1 нет бита для неё, линия снимается
// только действием на флаги/IE самого RTC.
static void mcu_stop_rtc_irq_handler(void)
{
    if (RTC->SR & RTC_SR_WUTF) {
        // Периодический тик: чистим WUTF (снимает линию), автоперезагрузка WUT
        // сама выстрелит в следующем периоде. Классификатор WUTF не читает.
        RTC->SCR = RTC_SCR_CWUTF;
        stop_wut_tick_pending = true;
    }
    if (RTC->SR & RTC_SR_ALRAF) {
        // Будильник: снимаем ALRAIE (снимает прямую линию EXTI19, линия =
        // ALRAF & ALRAIE), но ALRAF НЕ чистим — его защёлку читает опросчик
        // rtc_alarm_do_periodic_work в главном цикле. Будильник одноразовый и
        // завершает suspend, поэтому оставить ALRAIE замаскированным до конца
        // окна корректно.
        //
        // ВАЖНО: RTC_CR защищён WPR, а перед входом в Stop WPR ЗАБЛОКИРОВАН
        // (rtc_set_periodic_wakeup() при вооружении окна завершается
        // end_init_enable_wpr()). Голая запись `RTC->CR &= ~ALRAIE` здесь была
        // бы молча проигнорирована железом: ALRAIE осталась бы взведённой, линия
        // EXTI19 — асертнутой, и NVIC до бесконечности tail-chain'ил бы
        // RTC_TAMP_IRQ, так что запрошенное Linux пробуждение по будильнику
        // никогда бы не дошло до главного цикла (а IWDG досчитал бы до сброса).
        // Поэтому снимаем ALRAIE через WPR-разблокированный помощник rtc.c.
        rtc_mask_alarm_irq();
    }
}

// EXTI-линия 0 (кнопка PA0) — конфигурируемая: FPR1 чистится напрямую.
static void mcu_stop_exti0_1_irq_handler(void)
{
    if (EXTI->FPR1 & EXTI_FPR1_FPIF0) {
        EXTI->FPR1 = EXTI_FPR1_FPIF0;
        stop_button_wake_pending = true;
    }
}

void mcu_stop_window_prepare(void)
{
    stop_wut_tick_pending = false;
    stop_button_wake_pending = false;

    // RTC (Alarm A + WUT) — линия EXTI 19. Биты IE (WUTIE/ALRAIE) уже ставят
    // rtc_set_periodic_wakeup()/rtc_set_alarm(); не хватает только размаскировки
    // EXTI19 и NVIC — самая частая причина «RTC не будит из Stop» на G0.
    NVIC_SetHandler(RTC_TAMP_IRQn, mcu_stop_rtc_irq_handler);
    EXTI->IMR1 |= EXTI_IMR1_IM19;
    NVIC_EnableIRQ(RTC_TAMP_IRQn);

    // Кнопка PWRON (PA0, активна низким): фронт вниз = нажатие. PA0 по сбросу
    // уже замаплен на порт A в EXTICR, запись в EXTICR не нужна.
    NVIC_SetHandler(EXTI0_1_IRQn, mcu_stop_exti0_1_irq_handler);
    EXTI->FTSR1 |= EXTI_FTSR1_FT0;
    EXTI->IMR1 |= EXTI_IMR1_IM0;
    NVIC_EnableIRQ(EXTI0_1_IRQn);

    // Маскируем EXTI9 (SoC-CS/SPI2, оставлен размаскированным spi_slave_init):
    // SoC во время сна тёмный, ложное пробуждение по CS не нужно. Снимаем маску
    // только на выходе по реальному пробуждению (mcu_stop_window_finish).
    EXTI->IMR1 &= ~EXTI_IMR1_IM9;
}

void mcu_stop_window_finish(void)
{
    // Возвращаем прерывания к состоянию до окна сна, чтобы штатная работа шла
    // байт-в-байт как раньше: снимаем EXTI-источники пробуждения Stop
    // (кнопка EXTI0 и RTC EXTI19) и восстанавливаем маску SoC-CS EXTI9.
    EXTI->IMR1 &= ~(EXTI_IMR1_IM0 | EXTI_IMR1_IM19);
    NVIC_DisableIRQ(EXTI0_1_IRQn);
    NVIC_DisableIRQ(RTC_TAMP_IRQn);
    EXTI->IMR1 |= EXTI_IMR1_IM9;
    stop_wut_tick_pending = false;
    stop_button_wake_pending = false;
}

bool mcu_stop_take_wut_tick(void)
{
    bool ret = stop_wut_tick_pending;
    stop_wut_tick_pending = false;
    return ret;
}

bool mcu_stop_take_button_wake(void)
{
    bool ret = stop_button_wake_pending;
    stop_button_wake_pending = false;
    return ret;
}

void mcu_stop_enter(void)
{
    // E1: кормим IWDG прямо перед сном — максимальный запас до 10 с.
    watchdog_reload();

    // E2: сбрасываем ТОЛЬКО устаревшие не-будильниковые pending, чтобы не
    // провалиться мгновенно. НИКОГДА не CALRAF: реальный будильник обязан
    // разбудить (его защёлку читает опросчик, а не ISR).
    RTC->SCR = RTC_SCR_CWUTF;
    EXTI->FPR1 = EXTI_FPR1_FPIF0;
    PWR->SCR = PWR_SCR_CWUF;
    NVIC_ClearPendingIRQ(EXTI0_1_IRQn);

    // E3: Stop 1 (LPMS = 001)
    PWR->CR1 = (PWR->CR1 & ~PWR_CR1_LPMS_Msk) | PWR_CR1_LPMS_0;
    // E4: SLEEPDEEP
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    // E5: барьер + сон
    (void)PWR->CR1;
    __DSB();
    __WFI();
    // ---- Пробуждение: cause-aware ISR (тик WUT / будильник / кнопка) уже отработал ----

    // W1: сразу снова кормим IWDG.
    watchdog_reload();
    // W2: возвращаем обычный сон для следующего WFI.
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
    // W3: после Stop PLL выключен, SYSCLK = HSISYS 16 МГц — перезапираем 64 МГц.
    rcc_set_hsi_pll_64mhz_clock();
    // W4: LOAD systick рассчитан на 64 МГц — пересчитываем.
    systick_init();
    // W5: тактирование АЦП было остановлено — включаем и калибруем заново.
    adc_init(ADC_CLOCK_DIV_64, ADC_VREF_INT);
    // W6: перевзводим 100 мс окно «устаканивания» vmon, чтобы одиночный
    // несглаженный отсчёт V50 не увёл последовательность пробуждения в Standby.
    vmon_suspend_rearm_settle();
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
