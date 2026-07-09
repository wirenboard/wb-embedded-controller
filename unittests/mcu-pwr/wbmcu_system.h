#pragma once
// Локальный register-stub заголовок для теста mcu-pwr.
//
// Заменяет device-header реального билда (system/include/wbmcu_system.h ->
// stm32g030xx.h): даёт РЕАЛЬНОМУ src/mcu-pwr.c ровно те регистры, битовые маски,
// NVIC-обёртки и интринсики, которыми он пользуется, но перенаправляет
// экземпляры RTC/EXTI/PWR/SCB на обычную ОЗУ, которой управляет тест. Значения
// битовых масок скопированы дословно из system/include/stm32g030xx.h /
// system/cmsis/core_cm0plus.h, чтобы поведение совпадало с железом.
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ---- Номера прерываний (stm32g030xx.h) ----
typedef enum {
    RTC_TAMP_IRQn = 2,      // RTC через линию EXTI 19
    EXTI0_1_IRQn  = 5,      // EXTI 0 и 1
    SPI2_IRQn     = 26,     // SPI2 (regmap-движок; глушится на окно сна)
    UTEST_IRQn_COUNT = 32,
} IRQn_Type;

// ---- Периферийные структуры (только используемые поля) ----
typedef struct {
    volatile uint32_t CR1;
    volatile uint32_t SR1;
    volatile uint32_t SCR;
} PWR_TypeDef;

typedef struct {
    volatile uint32_t CR;
    volatile uint32_t SR;
    volatile uint32_t SCR;
    volatile uint32_t WPR;
} RTC_TypeDef;

typedef struct {
    volatile uint32_t IMR1;
    volatile uint32_t FTSR1;
    volatile uint32_t FPR1;
} EXTI_TypeDef;

typedef struct {
    volatile uint32_t SCR;
} SCB_TypeDef;

extern PWR_TypeDef  _utest_pwr;
extern RTC_TypeDef  _utest_rtc;
extern EXTI_TypeDef _utest_exti;
extern SCB_TypeDef  _utest_scb;

#define PWR  (&_utest_pwr)
#define RTC  (&_utest_rtc)
#define EXTI (&_utest_exti)
#define SCB  (&_utest_scb)

// ---- NVIC + интринсики (перехватываются тестом) ----
typedef void (*utest_irq_handler_t)(void);
extern utest_irq_handler_t _utest_nvic_handler[UTEST_IRQn_COUNT];

// NVIC_SetHandler в реальном билде пишет в vector_table[]; здесь запоминаем
// обработчик, чтобы тест мог вызвать ISR напрямую.
#define NVIC_SetHandler(irqn, handler) (_utest_nvic_handler[(irqn)] = (handler))

void NVIC_EnableIRQ(IRQn_Type irqn);
void NVIC_DisableIRQ(IRQn_Type irqn);
void NVIC_ClearPendingIRQ(IRQn_Type irqn);

void utest_stop_dsb(void);
void utest_stop_wfi(void);
#define __DSB() utest_stop_dsb()
#define __WFI() utest_stop_wfi()

// ---- Битовые маски (дословно из stm32g030xx.h / core_cm0plus.h) ----
#define PWR_SR1_WUF1_Pos          (0U)
#define PWR_SR1_SBF_Pos           (8U)
#define PWR_SR1_SBF_Msk           (0x1UL << PWR_SR1_SBF_Pos)                   // 0x00000100
#define PWR_SR1_SBF               PWR_SR1_SBF_Msk
#define PWR_SR1_WUFI_Pos          (15U)
#define PWR_SR1_WUFI_Msk          (0x1UL << PWR_SR1_WUFI_Pos)                  // 0x00008000
#define PWR_SR1_WUFI              PWR_SR1_WUFI_Msk

#define PWR_SCR_CWUF_Pos          (0U)
#define PWR_SCR_CWUF_Msk          (0x2BUL << PWR_SCR_CWUF_Pos)                 // 0x0000002B
#define PWR_SCR_CWUF              PWR_SCR_CWUF_Msk
#define PWR_SCR_CWUF1_Pos         (0U)
#define PWR_SCR_CWUF1_Msk         (0x1UL << PWR_SCR_CWUF1_Pos)                 // 0x00000001
#define PWR_SCR_CWUF1             PWR_SCR_CWUF1_Msk
#define PWR_SCR_CSBF_Pos          (8U)
#define PWR_SCR_CSBF_Msk          (0x1UL << PWR_SCR_CSBF_Pos)                  // 0x00000100
#define PWR_SCR_CSBF              PWR_SCR_CSBF_Msk

#define PWR_CR1_LPMS_Pos          (0U)
#define PWR_CR1_LPMS_Msk          (0x7UL << PWR_CR1_LPMS_Pos)                  // 0x00000007
#define PWR_CR1_LPMS              PWR_CR1_LPMS_Msk
#define PWR_CR1_LPMS_0            (0x1UL << PWR_CR1_LPMS_Pos)                  // 0x00000001
#define PWR_CR1_LPMS_1            (0x2UL << PWR_CR1_LPMS_Pos)                  // 0x00000002

#define RTC_CR_ALRAIE_Pos         (12U)
#define RTC_CR_ALRAIE_Msk         (0x1UL << RTC_CR_ALRAIE_Pos)                 // 0x00001000
#define RTC_CR_ALRAIE             RTC_CR_ALRAIE_Msk

#define RTC_SR_WUTF_Pos           (2U)
#define RTC_SR_WUTF_Msk           (0x1UL << RTC_SR_WUTF_Pos)                   // 0x00000004
#define RTC_SR_WUTF               RTC_SR_WUTF_Msk
#define RTC_SR_ALRAF_Pos          (0U)
#define RTC_SR_ALRAF_Msk          (0x1UL << RTC_SR_ALRAF_Pos)                  // 0x00000001
#define RTC_SR_ALRAF              RTC_SR_ALRAF_Msk

#define RTC_SCR_CWUTF_Pos         (2U)
#define RTC_SCR_CWUTF_Msk         (0x1UL << RTC_SCR_CWUTF_Pos)                 // 0x00000004
#define RTC_SCR_CWUTF             RTC_SCR_CWUTF_Msk
#define RTC_SCR_CALRAF_Pos        (0U)
#define RTC_SCR_CALRAF_Msk        (0x1UL << RTC_SCR_CALRAF_Pos)                // 0x00000001
#define RTC_SCR_CALRAF            RTC_SCR_CALRAF_Msk

#define EXTI_FTSR1_FT0_Pos        (0U)
#define EXTI_FTSR1_FT0_Msk        (0x1UL << EXTI_FTSR1_FT0_Pos)               // 0x00000001
#define EXTI_FTSR1_FT0            EXTI_FTSR1_FT0_Msk
#define EXTI_FPR1_FPIF0_Pos       (0U)
#define EXTI_FPR1_FPIF0_Msk       (0x1UL << EXTI_FPR1_FPIF0_Pos)             // 0x00000001
#define EXTI_FPR1_FPIF0           EXTI_FPR1_FPIF0_Msk
#define EXTI_IMR1_IM0_Pos         (0U)
#define EXTI_IMR1_IM0_Msk         (0x1UL << EXTI_IMR1_IM0_Pos)               // 0x00000001
#define EXTI_IMR1_IM0             EXTI_IMR1_IM0_Msk
#define EXTI_IMR1_IM9_Pos         (9U)
#define EXTI_IMR1_IM9_Msk         (0x1UL << EXTI_IMR1_IM9_Pos)               // 0x00000200
#define EXTI_IMR1_IM9             EXTI_IMR1_IM9_Msk
#define EXTI_IMR1_IM19_Pos        (19U)
#define EXTI_IMR1_IM19_Msk        (0x1UL << EXTI_IMR1_IM19_Pos)              // 0x00080000
#define EXTI_IMR1_IM19            EXTI_IMR1_IM19_Msk

#define SCB_SCR_SLEEPDEEP_Pos     (2U)
#define SCB_SCR_SLEEPDEEP_Msk     (1UL << SCB_SCR_SLEEPDEEP_Pos)              // 0x00000004
