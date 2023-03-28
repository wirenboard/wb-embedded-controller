#include "rtc.h"
#include "wbmcu_system.h"
#include "gpio.h"

/**
 * Модуль предоставляет доступ к RTC через функции записи-чтения
 * даты, времени, будильника и оффсета
 *
 * Данные отдаются в BCD формате
 */


// Время, которое будет установлено, если RTC на момент включения питания не работает
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
    RCC->APBENR1 |= RCC_APBENR1_PWREN | RCC_APBENR1_RTCAPBEN;
	PWR->CR1 |= PWR_CR1_DBP;

	RCC->BDCR |= RCC_BDCR_RTCEN;
	RCC->BDCR |= RCC_BDCR_RTCSEL_0;
	RCC->BDCR |= RCC_BDCR_LSEON;

    if (RTC->ICSR & RTC_ICSR_INITS) {
        // RTC already initialized, exit here
        return;
    }

    start_init_disable_wpr();

    // Set 24-hour format
    RTC->CR &= ~RTC_CR_FMT;

    set_datetime(&init_time);

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

    alarm->enabled = RTC->CR & RTC_CR_ALRAIE;
    alarm->flag = RTC->SR & RTC_SR_ALRAF;

    if (RTC->SR & RTC_SR_ALRAF) {
        RTC->SCR = RTC_SCR_CALRAF;
    }
}


void rtc_set_alarm(const struct rtc_alarm * alarm)
{
    uint32_t en = 0;
    if (!alarm->enabled) {
        en = RTC_ALRMAR_MSK1 | RTC_ALRMAR_MSK2 | RTC_ALRMAR_MSK3 | RTC_ALRMAR_MSK4;
    }

    start_init_disable_wpr();

    // Disable current alarm and interrupt
    RTC->CR &= ~(RTC_CR_ALRAIE | RTC_CR_ALRAE);

    // Wait for Alarm A write flag set
    while ((RTC->ICSR & RTC_ICSR_ALRAWF) == 0) {}

    RTC->ALRMAR =
        ((uint32_t)alarm->seconds << RTC_ALRMAR_SU_Pos) |
        ((uint32_t)alarm->minutes << RTC_ALRMAR_MNU_Pos) |
        ((uint32_t)alarm->hours << RTC_ALRMAR_HU_Pos) |
        ((uint32_t)alarm->days << RTC_ALRMAR_DU_Pos) |
        en;

    if (alarm->enabled) {
        RTC->CR |= RTC_CR_ALRAIE | RTC_CR_ALRAE;
    }

    end_init_enable_wpr();

    // Clear alarm flag
    RTC->SCR = RTC_SCR_CALRAF;
}

void rtc_clear_alarm_flag(void)
{
    RTC->SCR = RTC_SCR_CALRAF;
}

uint16_t rtc_get_offset(void)
{
    return RTC->CALR;
}

void rtc_set_offset(uint16_t offeset)
{
    start_init_disable_wpr();

    RTC->CALR = offeset;

    end_init_enable_wpr();
}

void rtc_enable_pc13_1hz_clkout(void)
{
    GPIO_SET_PUSHPULL(GPIOC, 13);
    GPIO_SET_OUTPUT(GPIOC, 13);

    start_init_disable_wpr();
    RTC->CR |= RTC_CR_COE | RTC_CR_COSEL;
    end_init_enable_wpr();
}

void rtc_disable_pc13_1hz_clkout(void)
{
    GPIO_SET_INPUT(GPIOC, 13);

    start_init_disable_wpr();
    RTC->CR &= ~(RTC_CR_COE | RTC_CR_COSEL);
    end_init_enable_wpr();
}
