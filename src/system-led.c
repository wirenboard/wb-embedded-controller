#include "system-led.h"
#include "regmap-structs.h"
#include "regmap-int.h"
#include "gpio.h"
#include "systick.h"
#include <stdbool.h>
#include "config.h"

#include "console.h" //DELME
/**
 * Модуль управляет системным светодиодом
 *
 * При первой попытке изменить состояние светодиода (светит/не светит) из Linux,
 * модуль автоматически переходит в режим управления с Linux-а
 * Обратно в режим EC модуль переходит в следующих случаях:
 *  - по команде из Linux-а
 *  - при перезагрузке/выключении Linux-а
 *  - при сигнале wathcdog-а из-за потери связи с Linux-ом 
 */

static const gpio_pin_t system_led_gpio = { EC_GPIO_LED };
static struct REGMAP_LED led_ctx;
static systime_t timestamp = 0;

void system_led_init(void)
{
    //Начальные настройки модуля:
    led_ctx.control = CONTROL_EC;
    led_ctx.mode = MODE_OFF;
    led_ctx.state = STATE_OFF;
    led_ctx.delay_on = 0;
    led_ctx.delay_off = 0;
    
    //Перекидываю эти настройки в REGMAP:
    regmap_set_region_data(REGMAP_REGION_LED, &led_ctx, sizeof(led_ctx));

    //Настройка портов:
    GPIO_S_SET_PUSHPULL(system_led_gpio);
    GPIO_S_SET_OUTPUT(system_led_gpio);
}

void system_led_set_control_from_ec(void)
{
    led_ctx.control = CONTROL_EC;
    regmap_set_region_data(REGMAP_REGION_LED, &led_ctx, sizeof(led_ctx));
}

static void set_state(enum led_state state)
{
    /* Устанавливаю програмное состояние диода и включаю/отключаю его свечение */
    if (led_ctx.state != state){ // Не дергаю состояние без необходимости
        led_ctx.state = state;
        if (state == STATE_ON) {
            #ifdef EC_GPIO_LED_ACTIVE_HIGH
            GPIO_S_SET(system_led_gpio);
            #else
            GPIO_S_RESET(system_led_gpio);
            #endif
        } else {
            #ifdef EC_GPIO_LED_ACTIVE_HIGH
            GPIO_S_RESET(system_led_gpio);
            #else
            GPIO_S_SET(system_led_gpio);
            #endif
        }
    }
    //Скармливаю LINUX-у текущее состояние:
    regmap_set_region_data(REGMAP_REGION_LED, &led_ctx, sizeof(led_ctx));
}

void system_led_disable(void)
{
    led_ctx.mode = MODE_OFF;
    set_state(STATE_OFF);
}

void system_led_enable(void)
{
    led_ctx.mode = MODE_ON;
    set_state(STATE_ON);
}

void system_led_blink(uint16_t on_ms, uint16_t off_ms)
{
    /* Метод для установки параметров мигания из под EC */
    if (led_ctx.control == CONTROL_EC){
        // Именяю состояние только в режиме EC
        led_ctx.mode = MODE_TIMER;
        led_ctx.delay_on = on_ms;
        led_ctx.delay_off = off_ms;
        timestamp = systick_get_system_time_ms();
        regmap_set_region_data(REGMAP_REGION_LED, &led_ctx, sizeof(led_ctx));
    }
}

void system_led_do_periodic_work(void)
{
    /* Здесь происходит вся магия управления свечением диода в текущий момент */

    // Проверяем, изменился ли регион LED в REGMAP извне:
    if (regmap_is_region_changed(REGMAP_REGION_LED))
    {
        // Обновленные значения из REGMAP:
        struct REGMAP_LED led_regmap;
        regmap_get_region_data(REGMAP_REGION_LED, &led_regmap, sizeof(led_regmap));

        if (led_regmap.control == CONTROL_EC){
            /* Когда из Linux вернули управление диодом обратно контроллеру */
            led_ctx.control = CONTROL_EC;
            // Остальные параметры оставляю на откуп системы
        } else {
            /* В любом другом случае любое изменение в REGMAP означает передачу 
             * управления светодиодом в Linux */
            led_ctx.control = CONTROL_LINUX;
            led_ctx.delay_on = led_regmap.delay_on;
            led_ctx.delay_off = led_regmap.delay_off;

            if (led_ctx.mode != led_regmap.mode){ 
                /* Устанавливаю режим работы светодиода */
                led_ctx.mode = led_regmap.mode;
                switch(led_ctx.mode){
                    case MODE_NONE: // При ручном управлении сперва включаю диод
                    case MODE_ON:
                        set_state(STATE_ON);
                        break;
                    case MODE_TIMER: // Мигание диодом
                        timestamp = systick_get_system_time_ms(); // Актуализирую тикалку
                        /* fall through */
                    default: // MODE_OFF
                        set_state(STATE_OFF); 
                }
            }else{
                /* Режим не поменялся, но изменились параметры мигания или 
                 * состояние свечения */
                if (led_ctx.mode == MODE_NONE){
                    // Даю изменять значение только в ручном управлении (NONE)
                    set_state(led_regmap.state);
                }
            }
        }
        regmap_set_region_data(REGMAP_REGION_LED, &led_ctx, sizeof(led_ctx));
        regmap_clear_changed(REGMAP_REGION_LED);
    }

    if (led_ctx.mode == MODE_TIMER){
        if (systick_get_time_since_timestamp(timestamp) < (
                led_ctx.state ? led_ctx.delay_on : led_ctx.delay_off
            )) {
            return;
        } else {
            set_state(!led_ctx.state);  // Ивертирую состояние
            timestamp = systick_get_system_time_ms(); // Обнуляю тикалку
        }
    }
}
