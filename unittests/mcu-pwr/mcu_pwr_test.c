#include "unity.h"

#include "mcu-pwr.h"
#include "config.h"
#include "utest_mcu_reg_env.h"

/**
 * Register-stub тест РЕАЛЬНОГО src/mcu-pwr.c.
 *
 * Интеграционный харнесс (unittests/wbec-integration) полностью МОКАЕТ mcu-pwr,
 * поэтому реальные ISR режима Stop, записи LPMS/SLEEPDEEP и снятие линии
 * будильника не исполнялись ни одним тестом — из-за чего blocker с застреванием
 * пробуждения по будильнику прошёл CI «зелёным». Здесь mcu-pwr.c собирается
 * поверх управляемых тестом регистров (RTC/EXTI/PWR/SCB), а NVIC/WFI
 * перехватываются, чтобы проверить именно поведение на уровне регистров/ISR.
 */

void setUp(void)
{
    utest_reg_env_reset();
}

void tearDown(void)
{
}

// ---- Вооружение окна: EXTI/NVIC источники пробуждения ----
// mcu_stop_window_prepare должен размаскировать EXTI19 (RTC) и EXTI0 (кнопка),
// завести падающий фронт на PA0, замаскировать EXTI9 (SoC-CS) и включить оба
// NVIC-вектора с зарегистрированными обработчиками.
static void test_stop_prepare_arms_exti_and_nvic(void)
{
    // spi_slave_init оставил EXTI9 (SoC-CS) размаскированным до входа в окно.
    EXTI->IMR1 = EXTI_IMR1_IM9;

    mcu_stop_window_prepare();

    TEST_ASSERT_TRUE_MESSAGE(EXTI->IMR1 & EXTI_IMR1_IM19,
        "RTC line EXTI19 must be unmasked");
    TEST_ASSERT_TRUE_MESSAGE(EXTI->IMR1 & EXTI_IMR1_IM0,
        "button line EXTI0 must be unmasked");
    TEST_ASSERT_TRUE_MESSAGE(EXTI->FTSR1 & EXTI_FTSR1_FT0,
        "button PA0 must trigger on the falling edge (press)");
    TEST_ASSERT_FALSE_MESSAGE(EXTI->IMR1 & EXTI_IMR1_IM9,
        "SoC-CS line EXTI9 must be masked for the sleep window");

    TEST_ASSERT_TRUE_MESSAGE(utest_nvic_is_enabled(RTC_TAMP_IRQn),
        "RTC_TAMP_IRQn must be NVIC-enabled");
    TEST_ASSERT_TRUE_MESSAGE(utest_nvic_is_enabled(EXTI0_1_IRQn),
        "EXTI0_1_IRQn must be NVIC-enabled");
    TEST_ASSERT_NOT_NULL_MESSAGE(utest_nvic_get_handler(RTC_TAMP_IRQn),
        "RTC ISR must be registered");
    TEST_ASSERT_NOT_NULL_MESSAGE(utest_nvic_get_handler(EXTI0_1_IRQn),
        "button ISR must be registered");
}

// ---- Вход в Stop 1: точные записи регистров сна ----
// Требования брифа §7: LPMS = 001 (Stop 1, НЕ 011/Standby), SLEEPDEEP взведён на
// момент сна, чистятся ТОЛЬКО устаревшие CWUTF/FPR0/CWUF, но НИКОГДА CALRAF,
// EXTI0 pending снимается в NVIC, IWDG кормится до и после сна.
static void test_stop_enter_writes_stop1_registers(void)
{
    // Мусор в LPMS до входа — должен быть перезаписан ровно на 001.
    PWR->CR1 = PWR_CR1_LPMS_0 | PWR_CR1_LPMS_1;   // 011 (Standby) — «до»

    mcu_stop_enter();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, utest_wfi_count(),
        "mcu_stop_enter must execute exactly one WFI");

    // LPMS = 001 (Stop 1), не 011 (Standby) — и на снимке WFI, и после.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(PWR_CR1_LPMS_0,
        utest_wfi_pwr_cr1() & PWR_CR1_LPMS_Msk,
        "LPMS must be 001 (Stop 1) at the moment of sleep");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(PWR_CR1_LPMS_0, PWR->CR1 & PWR_CR1_LPMS_Msk,
        "LPMS must remain 001 (Stop 1), never 011 (Standby)");

    // SLEEPDEEP взведён на момент сна (в W-фазе снимается — проверяем на снимке).
    TEST_ASSERT_TRUE_MESSAGE(utest_wfi_scb_scr() & SCB_SCR_SLEEPDEEP_Msk,
        "SLEEPDEEP must be set at the WFI");
    TEST_ASSERT_FALSE_MESSAGE(SCB->SCR & SCB_SCR_SLEEPDEEP_Msk,
        "SLEEPDEEP must be cleared again after wake");

    // Чистятся только устаревшие не-будильниковые флаги; ALRAF (CALRAF) — НИКОГДА.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(RTC_SCR_CWUTF, RTC->SCR,
        "only CWUTF must be written to RTC->SCR (drain stale WUT)");
    TEST_ASSERT_FALSE_MESSAGE(RTC->SCR & RTC_SCR_CALRAF,
        "CALRAF must NEVER be written on Stop entry (a real alarm must wake)");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(EXTI_FPR1_FPIF0, EXTI->FPR1,
        "stale button EXTI0 pending must be cleared");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(PWR_SCR_CWUF, PWR->SCR,
        "stale PWR wake flags must be cleared");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1,
        utest_nvic_get_clear_pending_count(EXTI0_1_IRQn),
        "stale EXTI0_1 NVIC pending must be cleared before sleep");

    // IWDG кормится и перед сном (E1), и сразу после (W1).
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(2, utest_watchdog_reload_count(),
        "IWDG must be fed both before and after the sleep");

    // Тактирование/периферия восстановлены после Stop.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, utest_rcc_restore_count(),
        "64 MHz clock must be restored after Stop");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, utest_systick_init_count(),
        "systick must be re-inited after Stop");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, utest_adc_init_count(),
        "ADC must be re-inited after Stop");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, utest_vmon_settle_count(),
        "vmon settle window must be re-armed after Stop");
}

// ---- BLOCKER-регрессия: ISR будильника снимает ALRAIE, СОХРАНЯЯ ALRAF ----
// Прямая линия EXTI19 = ALRAF & ALRAIE. При сработавшем будильнике ISR ОБЯЗАН
// снять ALRAIE (иначе линия висит и NVIC вечно tail-chain'ит RTC_TAMP_IRQ, а
// запрошенное Linux пробуждение по будильнику не доходит до главного цикла —
// именно этот отказ ронял Standby). При этом ALRAF чистить НЕЛЬЗЯ: его защёлку
// читает опросчик rtc_alarm_do_periodic_work. Снятие ALRAIE идёт через
// WPR-разблокированный помощник (голая запись RTC_CR под заблокированной WPR —
// no-op на железе).
static void test_stop_isr_alarm_masks_alraie_preserves_alraf(void)
{
    // Будильник вооружён (ALRAIE) и сработал (ALRAF защёлкнут).
    RTC->CR = RTC_CR_ALRAIE;
    RTC->SR = RTC_SR_ALRAF;
    RTC->SCR = 0;

    mcu_stop_window_prepare();
    utest_irq_handler_t rtc_isr = utest_nvic_get_handler(RTC_TAMP_IRQn);
    TEST_ASSERT_NOT_NULL(rtc_isr);
    rtc_isr();      // имитируем срабатывание RTC_TAMP_IRQ на пробуждении

    TEST_ASSERT_FALSE_MESSAGE(RTC->CR & RTC_CR_ALRAIE,
        "ISR must mask ALRAIE to dismiss the direct EXTI19 line");
    TEST_ASSERT_TRUE_MESSAGE(RTC->SR & RTC_SR_ALRAF,
        "ISR must PRESERVE ALRAF for the poller (never CALRAF)");
    TEST_ASSERT_FALSE_MESSAGE(RTC->SCR & RTC_SCR_CALRAF,
        "ISR must never write CALRAF");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, utest_rtc_mask_alarm_irq_calls(),
        "ALRAIE must be cleared via the WPR-unlocked rtc helper, not a bare CR write");
    TEST_ASSERT_TRUE_MESSAGE(utest_rtc_mask_alarm_irq_wpr_was_unlocked(),
        "WPR must be unlocked around the CR write (else HW drops it)");

    // Ни тик WUT, ни кнопка не должны взводиться будильниковым срабатыванием.
    TEST_ASSERT_FALSE_MESSAGE(mcu_stop_take_wut_tick(),
        "an alarm-only wake must not report a WUT tick");
    TEST_ASSERT_FALSE_MESSAGE(mcu_stop_take_button_wake(),
        "an alarm-only wake must not report a button wake");
}

// ---- ISR тика WUT: чистит только WUTF, не трогает будильник ----
static void test_stop_isr_wut_tick_clears_wutf_only(void)
{
    RTC->CR = RTC_CR_ALRAIE;    // будильник вооружён, но не сработал
    RTC->SR = RTC_SR_WUTF;      // истёк период WUT
    RTC->SCR = 0;

    mcu_stop_window_prepare();
    utest_irq_handler_t rtc_isr = utest_nvic_get_handler(RTC_TAMP_IRQn);
    TEST_ASSERT_NOT_NULL(rtc_isr);
    rtc_isr();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(RTC_SCR_CWUTF, RTC->SCR,
        "WUT tick must clear WUTF via SCR");
    TEST_ASSERT_TRUE_MESSAGE(RTC->CR & RTC_CR_ALRAIE,
        "WUT tick must not disturb the armed alarm (ALRAIE)");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, utest_rtc_mask_alarm_irq_calls(),
        "WUT tick must not touch the alarm irq helper");
    TEST_ASSERT_TRUE_MESSAGE(mcu_stop_take_wut_tick(),
        "WUT tick must be reported to the deadline accumulator");
    TEST_ASSERT_FALSE_MESSAGE(mcu_stop_take_button_wake(),
        "WUT tick must not report a button wake");
}

// ---- ISR кнопки (EXTI0): взводит button-wake, снимает FPR0 ----
static void test_stop_isr_button_edge_sets_pending(void)
{
    EXTI->FPR1 = EXTI_FPR1_FPIF0;   // защёлкнут падающий фронт PA0

    mcu_stop_window_prepare();
    utest_irq_handler_t exti_isr = utest_nvic_get_handler(EXTI0_1_IRQn);
    TEST_ASSERT_NOT_NULL(exti_isr);
    exti_isr();

    // Обработчик выполняет write-1-to-clear FPR0 (на железе снимает флаг).
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(EXTI_FPR1_FPIF0, EXTI->FPR1,
        "button ISR must write-1-to-clear the FPR0 pending flag");
    TEST_ASSERT_TRUE_MESSAGE(mcu_stop_take_button_wake(),
        "button edge must be reported as a button wake");
    TEST_ASSERT_FALSE_MESSAGE(mcu_stop_take_wut_tick(),
        "button edge must not report a WUT tick");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, utest_rtc_mask_alarm_irq_calls(),
        "button edge must not touch the alarm irq helper");
}

// ---- Выход окна: восстановление масок EXTI и выключение NVIC ----
static void test_stop_finish_restores_masks(void)
{
    EXTI->IMR1 = EXTI_IMR1_IM9;     // как до окна
    mcu_stop_window_prepare();      // размаскирует IM0/IM19, замаскирует IM9
    mcu_stop_window_finish();

    TEST_ASSERT_FALSE_MESSAGE(EXTI->IMR1 & (EXTI_IMR1_IM0 | EXTI_IMR1_IM19),
        "finish must re-mask the Stop wake lines (EXTI0, EXTI19)");
    TEST_ASSERT_TRUE_MESSAGE(EXTI->IMR1 & EXTI_IMR1_IM9,
        "finish must restore the SoC-CS mask (EXTI9)");
    TEST_ASSERT_FALSE_MESSAGE(utest_nvic_is_enabled(EXTI0_1_IRQn),
        "finish must disable the button NVIC line");
    TEST_ASSERT_FALSE_MESSAGE(utest_nvic_is_enabled(RTC_TAMP_IRQn),
        "finish must disable the RTC NVIC line");
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(1,
        utest_nvic_get_disable_count(RTC_TAMP_IRQn),
        "finish must actually issue NVIC_DisableIRQ for RTC");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_stop_prepare_arms_exti_and_nvic);
    RUN_TEST(test_stop_enter_writes_stop1_registers);
    RUN_TEST(test_stop_isr_alarm_masks_alraie_preserves_alraf);
    RUN_TEST(test_stop_isr_wut_tick_clears_wutf_only);
    RUN_TEST(test_stop_isr_button_edge_sets_pending);
    RUN_TEST(test_stop_finish_restores_masks);
    return UNITY_END();
}
