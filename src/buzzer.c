#include "buzzer.h"
#include "regmap-int.h"
#include "systick.h"

/**
 * Модуль управляет звуковым излучателем
 *
 * Управление возможно как из прошивки,
 * так и извне, через карту регистров.
 */

static struct REGMAP_BUZZER regmap_buzzer = {};
static uint16_t buzzer_start_time = 0;

void buzzer_init(void){
    // Инициализация GPIO и PWM 
     regmap_buzzer.enabled = 0;
}

void buzzer_start(uint16_t frequency, uint16_t duration, uint8_t volume){

    regmap_buzzer.frequency = frequency;
    regmap_buzzer.duration = duration;
    regmap_buzzer.volume = volume;
    regmap_buzzer.enabled = 1;

    buzzer_start_time = systick_get_system_time_ms();

    // утановка частоты и ширины импульса ШИМ 
    // исходя из значений "frequency" и "volume"
}

void buzzer_stop(){
    // останов PWM 

    regmap_buzzer.enabled = 0;
}

void buzzer_do_periodic_work(void)
{
    if (regmap_buzzer.enabled){
        if (systick_get_time_since_timestamp(buzzer_start_time) > regmap_buzzer.duration) {
            buzzer_stop();
        }
    }

    if (regmap_is_region_changed(REGMAP_REGION_BUZZER)) {
        struct REGMAP_BUZZER rm_buzzer;
        regmap_get_region_data(REGMAP_REGION_BUZZER, &rm_buzzer, sizeof(rm_buzzer));
        if(rm_buzzer.enabled){
            regmap_buzzer.frequency = rm_buzzer.frequency;
            regmap_buzzer.duration = rm_buzzer.duration;
            regmap_buzzer.volume = rm_buzzer.volume;
            regmap_buzzer.enabled = rm_buzzer.enabled;
        }
        else {
            buzzer_stop();
        }
        regmap_clear_changed(REGMAP_REGION_BUZZER);
    }
    regmap_set_region_data(REGMAP_REGION_BUZZER, &regmap_buzzer, sizeof(regmap_buzzer));
}
