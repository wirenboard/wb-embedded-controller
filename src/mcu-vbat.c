#include "mcu-vbat.h"
#include "config.h"
#include "wbmcu_system.h"
#include "adc.h"
#include "systick.h"
#include "fix16.h"
#include "regmap-int.h"

#define VBAT_THRESHOLD_MV               3000
#define VBAT_ADC_MEAS_PERIOD_MS         (1*24*3600*1000)    // 1 день между измерениями
#define VBAT_CHARGING_TIME_MS           (2*24*3600*1000)    // 2 дня длительность зарядки
// Время на стабилизацию делителя VBAT и набор свежего значения в DMA-буфере
#define VBAT_MEAS_STABILIZE_MS          30

enum vbat_state {
    VBAT_STATE_IDLE,        // ждём очередного периода измерения
    VBAT_STATE_MEASURING,   // делитель VBAT включён, ждём стабилизации
    VBAT_STATE_CHARGING,    // PWR_CR4_VBE включён, идёт зарядка
};

static enum vbat_state vbat_state;
static systime_t vbat_state_timestamp;
static int32_t vbat_mv;

static inline void update_regmap(void)
{
    struct REGMAP_VBAT_STATUS r;
    r.voltage_mv = vbat_mv;
    r.is_charging = (PWR->CR4 & PWR_CR4_VBE) ? 1 : 0;
    regmap_set_region_data(REGMAP_REGION_VBAT_STATUS, &r, sizeof(r));
}

void mcu_vbat_init(void)
{
    PWR->CR4 |= PWR_CR4_VBRS;   // для зарядки выбираем резистор 1.5 кОм, т.к. последовательно есть еще аппаратный резистор 3кОм
    vbat_state = VBAT_STATE_IDLE;
    // Установим timestamp так, чтобы первое измерение произошло сразу при включении
    vbat_state_timestamp = systick_get_system_time_ms() - VBAT_ADC_MEAS_PERIOD_MS;
}

void mcu_vbat_check_do_periodic_work(void)
{
    switch (vbat_state) {
    case VBAT_STATE_IDLE:
        if (systick_get_time_since_timestamp(vbat_state_timestamp) < VBAT_ADC_MEAS_PERIOD_MS) {
            break;
        }
        if (!adc_get_ready()) {
            break;
        }
        // Включаем встроенный делитель VBAT/3. Канал 14 уже сидит в DMA-списке,
        // АЦП конвертит его постоянно — теперь будет писать в raw_values реальное
        // значение батарейки.
        ADC->CCR |= ADC_CCR_VBATEN;
        vbat_state_timestamp = systick_get_system_time_ms();
        vbat_state = VBAT_STATE_MEASURING;
        break;

    case VBAT_STATE_MEASURING:
        if (systick_get_time_since_timestamp(vbat_state_timestamp) < VBAT_MEAS_STABILIZE_MS) {
            break;
        }
        // Защёлкиваем свежее raw в lowpass (иначе фильтр содержит мусор от
        // выключенного делителя), читаем mV, выключаем делитель для экономии батареи.
        adc_reset_lowpass(ADC_CHANNEL_ADC_INT_VBAT);
        vbat_mv = adc_get_ch_mv(ADC_CHANNEL_ADC_INT_VBAT);
        ADC->CCR &= ~ADC_CCR_VBATEN;
        vbat_state_timestamp = systick_get_system_time_ms();
        update_regmap();

        if (vbat_mv < VBAT_THRESHOLD_MV) {
            PWR->CR4 |= PWR_CR4_VBE;
            vbat_state = VBAT_STATE_CHARGING;
        } else {
            vbat_state = VBAT_STATE_IDLE;
        }
        break;

    case VBAT_STATE_CHARGING:
        if (systick_get_time_since_timestamp(vbat_state_timestamp) >= VBAT_CHARGING_TIME_MS) {
            PWR->CR4 &= ~PWR_CR4_VBE;
            vbat_state_timestamp = systick_get_system_time_ms();
            vbat_state = VBAT_STATE_IDLE;
        }
        break;
    }
}
