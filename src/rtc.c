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

// Стуктуры для доступа к битовым полям регистров
union rtc_tr_reg {
    uint32_t reg_val;
    struct {
        uint32_t su_st : 7;
        uint32_t res1 : 1;
        uint32_t mnu_mnt : 7;
        uint32_t res2 : 1;
        uint32_t hu_ht : 6;
        uint32_t pm : 1;
    } reg_descr;
};

union rtc_dr_reg {
    uint32_t reg_val;
    struct {
        uint32_t du_dt : 6;
        uint32_t res1 : 2;
        uint32_t mu_mt : 5;
        uint32_t wdu : 3;
        uint32_t yu_yt : 8;
    } reg_descr;
};

union rtc_alrmx_reg {
    uint32_t reg_val;
    struct {
        uint32_t su_st : 7;
        uint32_t msk_secs : 1;
        uint32_t mnu_mnt : 7;
        uint32_t msk_mins : 1;
        uint32_t hu_ht : 6;
        uint32_t pm : 1;
        uint32_t msk_hours : 1;
        uint32_t du_dt : 6;
        uint32_t wdsel : 1;
        uint32_t msk_date : 1;
    } reg_descr;
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
    union rtc_tr_reg tr;
    tr.reg_val = 0;
    tr.reg_descr.su_st = tm->seconds;
    tr.reg_descr.mnu_mnt = tm->minutes;
    tr.reg_descr.hu_ht = tm->hours;
    RTC->TR = tr.reg_val;

    union rtc_dr_reg dr;
    dr.reg_val = 0;
    dr.reg_descr.wdu = tm->weekdays;
    dr.reg_descr.du_dt = tm->days;
    dr.reg_descr.mu_mt = tm->months;
    dr.reg_descr.yu_yt = tm->years;
    RTC->DR = dr.reg_val;
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
    union rtc_tr_reg tr;
    tr.reg_val = RTC->TR;
    tm->seconds = tr.reg_descr.su_st;
    tm->minutes = tr.reg_descr.mnu_mnt;
    tm->hours = tr.reg_descr.hu_ht;

    union rtc_dr_reg dr;
    dr.reg_val = RTC->DR;
    tm->weekdays = dr.reg_descr.wdu;
    tm->days = dr.reg_descr.du_dt;
    tm->months = dr.reg_descr.mu_mt;
    tm->years = dr.reg_descr.yu_yt;
}

void rtc_set_datetime(const struct rtc_time * tm)
{
    start_init_disable_wpr();

    set_datetime(tm);

    end_init_enable_wpr();
}

void rtc_get_alarm(struct rtc_alarm * alarm)
{
    union rtc_alrmx_reg al;
    al.reg_val = RTC->ALRMAR;

    alarm->seconds = al.reg_descr.su_st;
    alarm->minutes = al.reg_descr.mnu_mnt;
    alarm->hours = al.reg_descr.hu_ht;
    alarm->days = al.reg_descr.du_dt;

    alarm->enabled = RTC->CR & RTC_CR_ALRAIE;
    alarm->flag = RTC->SR & RTC_SR_ALRAF;

    if (RTC->SR & RTC_SR_ALRAF) {
        RTC->SCR = RTC_SCR_CALRAF;
    }
}


void rtc_set_alarm(const struct rtc_alarm * alarm)
{
    start_init_disable_wpr();

    // Disable current alarm and interrupt
    RTC->CR &= ~(RTC_CR_ALRAIE | RTC_CR_ALRAE);

    union rtc_alrmx_reg al;
    al.reg_val = 0;
    if (!alarm->enabled) {
        al.reg_descr.msk_secs = 1;
        al.reg_descr.msk_mins = 1;
        al.reg_descr.msk_hours = 1;
        al.reg_descr.msk_date = 1;
    }
    al.reg_descr.su_st = alarm->seconds;
    al.reg_descr.mnu_mnt = alarm->minutes;
    al.reg_descr.hu_ht = alarm->hours;
    al.reg_descr.du_dt = alarm->days;

    // Wait for Alarm A write flag set
    while ((RTC->ICSR & RTC_ICSR_ALRAWF) == 0) {}

    RTC->ALRMAR = al.reg_val;

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
