#include "systick.h"
#include "stm32g0xx.h"
#include "config.h"

static systime_t system_time = 0;

void systick_init(void)
{
    system_time = 0;

    SysTick->VAL = 0;
    SysTick->LOAD = F_CPU / 8 / 1000 - 1;
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk;
    NVIC_EnableIRQ(SysTick_IRQn);
}

systime_t systick_get_system_time(void)
{
    return system_time;
}

systime_t systick_get_time_since_timestamp(systime_t timestamp)
{
    return (int32_t)(system_time - timestamp);
}

void SysTick_Handler(void)
{
    system_time++;
}
