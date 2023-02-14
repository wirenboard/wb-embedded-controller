#include "rtc.h"
#include "stm32g0xx.h"
#include "gpio.h"

#define CALIB_TIM               TIM17
#define CALIB_TIM_CLOCK_EN()    do { RCC->APBENR2 |= RCC_APBENR2_TIM17EN; } while (0)

uint32_t array[500];
uint16_t counter = 0;

enum meas_status {
    MEAS_STOPPED,
    MEAS_IN_PROGRESS,
    MEAS_FINISHED,
    MEAS_ERROR,
};

struct meas_ctx {
    TIM_TypeDef *tim;
    enum meas_status status;
    bool pulse_captured;
    uint16_t uif_cnt;
    uint32_t period;
    uint16_t start_crr;
};

struct meas_ctx ref_meas_ctx;
struct meas_ctx rtc_meas_ctx;

static const struct rtc_time init_time = {
    .seconds = 0x00,
    .minutes = 0x00,
    .hours = 0x00,
    .weekdays = 0x1,
    .days = 0x01,
    .months = 0x01,
    .years = 0x20
};

static inline void disable_wpr(void)
{
    RTC->WPR = 0xCA;
    RTC->WPR = 0x53;
}

static inline void enable_wpr(void)
{
    RTC->WPR = 0;
}

static inline void start_init_disable_wpr(void)
{
    disable_wpr();

    RTC->ICSR |= RTC_ICSR_INIT;
    while ((RTC->ICSR & RTC_ICSR_INITF) == 0) {}
}

static inline void end_init_enable_wpr(void)
{
    RTC->ICSR &= ~RTC_ICSR_INIT;
    while (RTC->ICSR & RTC_ICSR_INITF) {}

    enable_wpr();
}

static inline void reset_meas_context(struct meas_ctx *ctx)
{
    ctx->status = MEAS_STOPPED;
    ctx->pulse_captured = 0;
    ctx->uif_cnt = 0;
    ctx->start_crr = 0;
    ctx->period = 0;
}

static void set_datetime(const struct rtc_time * tm)
{
    RTC->TR =
        ((uint32_t)tm->seconds << RTC_TR_SU_Pos) |
        ((uint32_t)tm->minutes << RTC_TR_MNU_Pos) |
        ((uint32_t)tm->hours << RTC_TR_HU_Pos);

    RTC->DR =
        ((uint32_t)tm->weekdays << RTC_DR_WDU_Pos) |
        ((uint32_t)tm->days << RTC_DR_DU_Pos) |
        ((uint32_t)tm->months << RTC_DR_MU_Pos) |
        ((uint32_t)tm->years << RTC_DR_YU_Pos);
}

void rtc_init(void)
{
    reset_meas_context(&ref_meas_ctx);
    reset_meas_context(&rtc_meas_ctx);

    // Set RTC output on PA4
    GPIO_SET_PUSHPULL(GPIOA, 4);
    GPIO_SET_OUTPUT(GPIOA, 4);
    GPIO_SET_AF(GPIOA, 4, 7);

    RCC->APBENR1 |= RCC_APBENR1_PWREN | RCC_APBENR1_RTCAPBEN;
	PWR->CR1 |= PWR_CR1_DBP;

	RCC->BDCR |= RCC_BDCR_RTCEN;
	RCC->BDCR |= RCC_BDCR_RTCSEL_0;
	RCC->BDCR |= RCC_BDCR_LSEON;

    if (RTC->ICSR & RTC_ICSR_INITS) {
        // RTC already initialized
        return;
    }

    // TODO Set power loss bit?

    start_init_disable_wpr();

    // Set prescalers
    // RTC->PRER = 128 << RTC_PRER_PREDIV_A_Pos | 256 << RTC_PRER_PREDIV_S_Pos;

    // Set 24-hour format
    RTC->CR &= ~RTC_CR_FMT;

    // Set RTC output on PA4
    RTC->CR |= RTC_CR_COE | RTC_CR_OUT2EN | RTC_CR_COSEL;

    set_datetime(&init_time);

    // Set alarm
    RTC->ALRMAR = RTC_ALRMAR_MSK2 | RTC_ALRMAR_MSK3 | RTC_ALRMAR_MSK4;
    RTC->ALRMAR |= 0x10;
    // Enable alarm
    RTC->CR |= RTC_CR_ALRAE;

    end_init_enable_wpr();
}


bool rtc_get_ready_read(void)
{
    if (RTC->ICSR & RTC_ICSR_RSF) {
        RTC->ICSR = 0;
        return 1;
    }

    return 0;
}

void rtc_get_datetime(struct rtc_time * tm)
{
    uint32_t tr = RTC->TR;
    uint32_t dr = RTC->DR;

    tm->seconds = (tr & (RTC_TR_SU_Msk | RTC_TR_ST_Msk)) >> RTC_TR_SU_Pos;
    tm->minutes = (tr & (RTC_TR_MNU_Msk | RTC_TR_MNT_Msk)) >> RTC_TR_MNU_Pos;
    tm->hours = (tr & (RTC_TR_HU_Msk | RTC_TR_HT_Msk)) >> RTC_TR_HU_Pos;

    tm->weekdays = (dr & (RTC_DR_WDU_Msk)) >> RTC_DR_WDU_Pos;
    tm->days = (dr & (RTC_DR_DU_Msk | RTC_DR_DT_Msk)) >> RTC_DR_DU_Pos;
    tm->months = (dr & (RTC_DR_MU_Msk | RTC_DR_MT_Msk)) >> RTC_DR_MU_Pos;
    tm->years = (dr & (RTC_DR_YU_Msk | RTC_DR_YT_Msk)) >> RTC_DR_YU_Pos;


    if (RTC->SR & RTC_SR_ALRAF) {
        RTC->SCR = RTC_SCR_CALRAF;
    }
}

void rtc_set_datetime(const struct rtc_time * tm)
{
    start_init_disable_wpr();

    set_datetime(tm);

    end_init_enable_wpr();
}

void rtc_get_alarm(struct rtc_alarm * alarm)
{
    uint32_t al = RTC->ALRMAR;

    alarm->seconds = (al & (RTC_ALRMAR_SU_Msk | RTC_ALRMAR_ST_Msk)) >> RTC_ALRMAR_SU_Pos;
    alarm->minutes = (al & (RTC_ALRMAR_MNU_Msk | RTC_ALRMAR_MNT_Msk)) >> RTC_ALRMAR_MNU_Pos;
    alarm->hours = (al & (RTC_ALRMAR_HU_Msk | RTC_ALRMAR_HT_Msk)) >> RTC_ALRMAR_HU_Pos;
    alarm->days = (al & (RTC_ALRMAR_DU_Msk | RTC_ALRMAR_DT_Msk)) >> RTC_ALRMAR_DU_Pos;

    alarm->enabled =
        ((al & RTC_ALRMAR_MSK1) == 0) ||
        ((al & RTC_ALRMAR_MSK2) == 0) ||
        ((al & RTC_ALRMAR_MSK3) == 0) ||
        ((al & RTC_ALRMAR_MSK4) == 0);
}


void rtc_set_alarm(const struct rtc_alarm * alarm)
{
    uint32_t en = 0;
    if (!alarm->enabled) {
        en = RTC_ALRMAR_MSK1 | RTC_ALRMAR_MSK2 | RTC_ALRMAR_MSK3 | RTC_ALRMAR_MSK4;
    }

    start_init_disable_wpr();

    RTC->ALRMAR =
        ((uint32_t)alarm->seconds << RTC_ALRMAR_SU_Pos) |
        ((uint32_t)alarm->minutes << RTC_ALRMAR_MNU_Pos) |
        ((uint32_t)alarm->hours << RTC_ALRMAR_HU_Pos) |
        ((uint32_t)alarm->days << RTC_ALRMAR_DU_Pos) |
        en;

    end_init_enable_wpr();
}

void rtc_start_calibration(void)
{
    counter = 0;
    
    reset_meas_context(&ref_meas_ctx);
    reset_meas_context(&rtc_meas_ctx);
    
    RCC->APBENR2 |= RCC_APBENR2_TIM14EN;
    
    TIM14->CNT = 0;
    TIM14->PSC = 0;
    TIM14->ARR = 0xFFFF;
    TIM14->TISEL = 0b0001;      // RTC CLK
    TIM14->CCMR1 = TIM_CCMR1_CC1S_0 * 0b01;
    TIM14->CCER |= TIM_CCER_CC1E;
    TIM14->DIER |= TIM_DIER_UIE | TIM_DIER_CC1IE;
    TIM14->SR = 0;
    NVIC_EnableIRQ(TIM14_IRQn);
    NVIC_SetPriority(TIM14_IRQn, 0);
    
    TIM14->CR1 |= TIM_CR1_CEN;
    
    
    return;

    // Init PB9 GPIO
    GPIO_SET_INPUT(GPIOB, 9);
    GPIO_SET_AF(GPIOB, 9, 2);

    // Init CALIB_TIB
    CALIB_TIM->CNT = 0;
    CALIB_TIM->PSC = 8 - 1;
    CALIB_TIM->ARR = 0xFFFF;
    CALIB_TIM->CCMR1 |= TIM_CCMR1_CC1S_0 * 0b01;
    CALIB_TIM->CCER |= TIM_CCER_CC1E;
    CALIB_TIM->DIER |= TIM_DIER_UIE | TIM_CCER_CC1E;
    TIM16->SR = 0;
    NVIC_EnableIRQ(TIM17_IRQn);
    NVIC_SetPriority(TIM17_IRQn, 0);
    
    CALIB_TIM->CR1 |= TIM_CR1_CEN;
    
    
    // Init TIM16 to capture wakeup events
    RCC->APBENR2 |= RCC_APBENR2_TIM16EN;

    TIM16->CNT = 0;
    TIM16->PSC = 0;
    TIM16->ARR = 0xFFFF;
    TIM16->TISEL = 0b0011;      // RTC wakeup
    TIM16->CCMR1 = TIM_CCMR1_CC1S_0 * 0b01;
    TIM16->CCER |= TIM_CCER_CC1E;
    TIM16->DIER |= TIM_DIER_UIE | TIM_DIER_CC1IE;
    TIM16->SR = 0;
    NVIC_EnableIRQ(TIM16_IRQn);
    NVIC_SetPriority(TIM16_IRQn, 0);

    // Enable RTC auto-wakeup signal with 1 s period
    disable_wpr();

    RTC->CR &= ~RTC_CR_WUTE;
    while ((RTC->ICSR & RTC_ICSR_WUTWF) == 0) {}
    // Set ck_wut wakeup clock
    RTC->CR &= ~RTC_CR_WUCKSEL;
    RTC->CR |= RTC_CR_WUCKSEL_0 * 0b011;    // RTC/2 clock is selected
    RTC->WUTR = 32768 / 2 - 1;
    RTC->CR |= RTC_CR_WUTE | RTC_CR_WUTIE;

    enable_wpr();

    RTC->SCR = RTC_SCR_CWUTF;

    TIM16->CR1 |= TIM_CR1_CEN;
}

void TIM17_IRQHandler(void)
{
    if (CALIB_TIM->SR & TIM_SR_UIF) {
        CALIB_TIM->SR = ~TIM_SR_UIF;
        ref_meas_ctx.uif_cnt++;

        // Check max events (no signal)
    }

    if (CALIB_TIM->SR & TIM_SR_CC1IF) {
        CALIB_TIM->SR = ~TIM_SR_CC1IF;
        // Reference pulse detected, skip first
        uint16_t ccr = CALIB_TIM->CCR1;
        if (!ref_meas_ctx.pulse_captured) {
            ref_meas_ctx.pulse_captured = 1;
            ref_meas_ctx.uif_cnt = 0;
            ref_meas_ctx.start_crr = ccr;
        } else {
            ref_meas_ctx.period = (65536 * ref_meas_ctx.uif_cnt + ccr) - ref_meas_ctx.start_crr;
            ref_meas_ctx.status = MEAS_FINISHED;
            //
            RCC->APBRSTR2 |= RCC_APBRSTR2_TIM17RST;
            RCC->APBRSTR2 &= ~RCC_APBRSTR2_TIM17RST;
            RCC->APBENR2 &= ~RCC_APBENR2_TIM17EN;
        }
    }
}

void TIM16_IRQHandler(void)
{
    if (TIM16->SR & TIM_SR_UIF) {
        TIM16->SR = ~TIM_SR_UIF;

        rtc_meas_ctx.uif_cnt++;

        // Check max events (no signal)
    }

    if (TIM16->SR & TIM_SR_CC1IF) {
        TIM16->SR = ~TIM_SR_CC1IF;
        // Reference pulse detected, skip first
        uint16_t ccr = TIM16->CCR1;

        // Clear RTC wakeup flag
        RTC->SCR = RTC_SCR_CWUTF;

        if (!rtc_meas_ctx.pulse_captured) {
            rtc_meas_ctx.pulse_captured = 1;
            rtc_meas_ctx.uif_cnt = 0;
            rtc_meas_ctx.start_crr = ccr;
        } else {
            rtc_meas_ctx.period = (65536 * rtc_meas_ctx.uif_cnt + ccr) - rtc_meas_ctx.start_crr;
            
            if (counter < 500) {
                array[counter] = rtc_meas_ctx.period;
                rtc_meas_ctx.start_crr = ccr;
                rtc_meas_ctx.uif_cnt = 0;
                counter++;
            } else {
                rtc_meas_ctx.status = MEAS_FINISHED;
                //
                RCC->APBRSTR2 |= RCC_APBRSTR2_TIM16RST;
                RCC->APBRSTR2 &= ~RCC_APBRSTR2_TIM16RST;
                RCC->APBENR2 &= ~RCC_APBENR2_TIM16EN;
            }
        }
    }
}

void TIM14_IRQHandler(void)
{
    if (TIM14->SR & TIM_SR_UIF) {
        TIM14->SR = ~TIM_SR_UIF;

        //rtc_meas_ctx.uif_cnt++;
        rtc_meas_ctx.pulse_captured = 0;

        // Check max events (no signal)
    }

    if (TIM14->SR & TIM_SR_CC1IF) {
        TIM14->SR = ~TIM_SR_CC1IF;
        // Reference pulse detected, skip first
        uint16_t ccr = TIM14->CCR1;
        

        if (!rtc_meas_ctx.pulse_captured) {
            rtc_meas_ctx.pulse_captured = 1;
            rtc_meas_ctx.uif_cnt = 0;
            rtc_meas_ctx.start_crr = ccr;
        } else {
            rtc_meas_ctx.period = ccr - rtc_meas_ctx.start_crr;
            rtc_meas_ctx.start_crr = ccr;
            
            if (counter < 500) {
                array[counter] = rtc_meas_ctx.period;
                counter++;
            } else {
                RCC->APBRSTR2 |= RCC_APBRSTR2_TIM14RST;
                RCC->APBRSTR2 &= ~RCC_APBRSTR2_TIM14RST;
                RCC->APBENR2 &= ~RCC_APBENR2_TIM14EN;
            }
            //rtc_meas_ctx.status = MEAS_FINISHED;
            //
            //RCC->APBRSTR2 |= RCC_APBRSTR2_TIM14RST;
            //RCC->APBRSTR2 &= ~RCC_APBRSTR2_TIM14RST;
            //RCC->APBENR2 &= ~RCC_APBENR2_TIM14EN;
        }
    }
}
