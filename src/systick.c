#include "systick.h"
#include "wbmcu_system.h"
#include "config.h"

/**
 * Модуль считает время с дискретностью 1 мс
 * Используется SysTick таймер
 *
 * Функция systick_get_time_since_timestamp позволяет получить время,
 * прошедшее с момента сохраненной метки времени
 */

static systime_t system_time = 0;

static void systick_irq_handler(void)
{
    system_time++;
}

void systick_init(void)
{
    system_time = 0;

    SysTick->VAL = 0;
    SysTick->LOAD = F_CPU / 8 / 1000 - 1;
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk;
    NVIC_SetHandler(SysTick_IRQn, systick_irq_handler);
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
