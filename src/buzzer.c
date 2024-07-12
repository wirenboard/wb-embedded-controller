#include "buzzer.h"

#if defined EC_GPIO_BUZZER

#include "wbmcu_system.h"
#include "gpio.h"
#include "systick.h"
#include "regmap-int.h"
#include "rcc.h"
#include <stdbool.h>

static const gpio_pin_t buzzer_gpio = { EC_GPIO_BUZZER };

struct buzzer_ctx {
    systime_t beep_start_time;
    uint16_t beep_duration_ms;
    bool beep_in_progress;
};

static struct buzzer_ctx buzzer_ctx = {};

static inline void buzzer_disable(void)
{
    TIM3->CCR2 = 0;
}

static void buzzer_enable(uint16_t freq_hz, uint16_t duty_percent)
{
    // calc PSC and ARR values
    uint32_t divider = SystemCoreClock / freq_hz;
    uint16_t psc = divider >> 16;
    uint16_t arr = divider / (psc + 1) - 1;
    uint16_t ccr = arr * duty_percent / 100;

    TIM3->PSC = psc;
    TIM3->ARR = arr;
    TIM3->CCR2 = ccr;
    TIM3->EGR = TIM_EGR_UG;
}

void buzzer_init(void)
{
    GPIO_S_RESET(buzzer_gpio);
    GPIO_S_SET_OUTPUT(buzzer_gpio);
    GPIO_S_SET_PUSHPULL(buzzer_gpio);
    GPIO_S_SET_AF(buzzer_gpio, 1);

    RCC->APBENR1 |= RCC_APBENR1_TIM3EN;
    // use ch2
    TIM3->CCMR1 = TIM_CCMR1_OC2M_1 | TIM_CCMR1_OC2M_2;
    TIM3->CCER = TIM_CCER_CC2E;
    TIM3->CR1 = TIM_CR1_CEN;
}

void buzzer_beep(uint16_t freq, uint16_t duration_ms)
{
    buzzer_ctx.beep_in_progress = true;
    buzzer_ctx.beep_start_time = systick_get_system_time_ms();
    buzzer_ctx.beep_duration_ms = duration_ms;
    buzzer_enable(freq, 50);
}

void buzzer_subsystem_do_periodic_work(void)
{
    if (regmap_is_region_changed(REGMAP_REGION_BUZZER_CTRL)) {
        struct REGMAP_BUZZER_CTRL buzzer_ctrl;
        regmap_get_region_data(REGMAP_REGION_BUZZER_CTRL, &buzzer_ctrl, sizeof(buzzer_ctrl));

        if (buzzer_ctrl.enabled) {
            buzzer_enable(buzzer_ctrl.freq_hz, buzzer_ctrl.duty_percent);
        } else {
            buzzer_disable();
        }
        regmap_clear_changed(REGMAP_REGION_BUZZER_CTRL);
    }

    if (buzzer_ctx.beep_in_progress) {
        unsigned beep_elapsed_time = systick_get_time_since_timestamp(buzzer_ctx.beep_start_time);
        if (beep_elapsed_time >= buzzer_ctx.beep_duration_ms) {
            buzzer_disable();
            buzzer_ctx.beep_in_progress = false;
        }
    }
}

#endif
