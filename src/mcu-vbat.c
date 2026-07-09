#include "mcu-vbat.h"
#include "config.h"
#include "wbmcu_system.h"
#include "adc.h"
#include "systick.h"
#include "fix16.h"
#include "regmap-int.h"
#include <stdbool.h>

#define VBAT_THRESHOLD_MV               2850                // 2.85 В - порог, ниже которого считаем батарейку разряженной и запускаем зарядку
#define VBAT_ADC_MEAS_PERIOD_MS         (1*24*3600*1000)    // 1 день между измерениями
#define VBAT_CHARGING_TIME_MS           (2*3600*1000)       // 2 часа длительность зарядки
// Время на стабилизацию делителя VBAT и набор свежего значения в DMA-буфере
#define VBAT_MEAS_STABILIZE_MS          100
// Пауза после выключения заряда перед измерением
#define VBAT_RELAX_TIME_MS              (5*60*1000)         // 5 минут

enum vbat_state {
    VBAT_STATE_IDLE,        // ждём очередного периода измерения
    VBAT_STATE_MEASURING,   // делитель VBAT включён, ждём стабилизации
    VBAT_STATE_CHARGING,    // PWR_CR4_VBE включён, идёт зарядка
    VBAT_STATE_RELAX,       // заряд выключен, ждём релаксации перед измерением
};

static enum vbat_state vbat_state;
// Якорь суточной сетки: момент последнего измерения. По нему отсчитывается
// начало следующего суточного интервала (не сдвигается зарядкой).
static systime_t vbat_meas_timestamp;
// Момент начала текущей зарядки, для отсчёта её длительности.
static systime_t vbat_charge_timestamp;
// Момент включения делителя, для отсчёта времени стабилизации в MEASURING.
static systime_t vbat_stabilize_timestamp;
// Измерение сразу после окончания зарядки (для фиксации результата): заряд по
// его итогам повторно не запускаем, ждём следующего суточного интервала.
static bool vbat_meas_after_charge;
static int32_t vbat_mv;

static inline void start_charge(void)
{
    PWR->CR4 |= PWR_CR4_VBE;
}

static inline void stop_charge(void)
{
    PWR->CR4 &= ~PWR_CR4_VBE;
}

static inline bool is_charging(void)
{
    if (PWR->CR4 & PWR_CR4_VBE) {
        return true;
    } else {
        return false;
    }
}

// Запускает измерение: включает делитель VBAT/3 и переводит в MEASURING.
// Возвращает false, если АЦП ещё не готов (нужно повторить попытку позже).
static bool start_measurement(void)
{
    if (!adc_get_ready()) {
        return false;
    }
    // Включаем встроенный делитель VBAT/3. Канал 14 уже сидит в DMA-списке,
    // АЦП конвертит его постоянно — теперь будет писать в raw_values реальное
    // значение батарейки.
    ADC->CCR |= ADC_CCR_VBATEN;
    // Защёлкиваем последнее raw в lowpass (иначе фильтр содержит мусор от выключенного делителя)
    adc_reset_lowpass(ADC_CHANNEL_ADC_INT_VBAT);
    vbat_stabilize_timestamp = systick_get_system_time_ms();
    vbat_state = VBAT_STATE_MEASURING;
    return true;
}

static inline void update_regmap(void)
{
    struct REGMAP_VBAT_STATUS r;
    r.voltage_mv = vbat_mv;
    if (is_charging()) {
        r.is_charging = 1;
    } else {
        r.is_charging = 0;
    }
    regmap_set_region_data(REGMAP_REGION_VBAT_STATUS, &r, sizeof(r));
}

void mcu_vbat_init(void)
{
    PWR->CR4 |= PWR_CR4_VBRS;   // для зарядки выбираем резистор 1.5 кОм, т.к. последовательно есть еще аппаратный резистор 3кОм
    vbat_state = VBAT_STATE_IDLE;
    // Установим timestamp так, чтобы первое измерение произошло сразу при включении
    vbat_meas_timestamp = systick_get_system_time_ms() - VBAT_ADC_MEAS_PERIOD_MS;
}

void mcu_vbat_check_do_periodic_work(void)
{
    switch (vbat_state) {
    case VBAT_STATE_IDLE:
        if (systick_get_time_since_timestamp(vbat_meas_timestamp) < VBAT_ADC_MEAS_PERIOD_MS) {
            break;
        }
        // Якорь суточной сетки ставим на момент начала измерения: следующее
        // измерение произойдёт ровно через сутки независимо от того, была ли зарядка.
        vbat_meas_timestamp = systick_get_system_time_ms();
        vbat_meas_after_charge = false;
        start_measurement();    // если АЦП не готов, останемся в IDLE и попробуем на след. вызове
        break;

    case VBAT_STATE_MEASURING:
        if (systick_get_time_since_timestamp(vbat_stabilize_timestamp) < VBAT_MEAS_STABILIZE_MS) {
            break;
        }
        // читаем mV, выключаем делитель для экономии батареи
        vbat_mv = adc_get_ch_mv(ADC_CHANNEL_ADC_INT_VBAT);
        ADC->CCR &= ~ADC_CCR_VBATEN;

        // После зарядки измеряем только для фиксации результата — заряд повторно
        // не запускаем, ждём следующего суточного интервала.
        if ((vbat_mv < VBAT_THRESHOLD_MV) && (!vbat_meas_after_charge)) {
            start_charge();
            vbat_charge_timestamp = systick_get_system_time_ms();
            vbat_state = VBAT_STATE_CHARGING;
        } else {
            vbat_state = VBAT_STATE_IDLE;
        }
        break;

    case VBAT_STATE_CHARGING:
        // Заряжаем 2 часа, затем выключаем заряд и даём батарейке релаксировать
        // перед контрольным измерением. Следующий суточный интервал отсчитывается
        // от vbat_meas_timestamp, зарядкой не сдвигается.
        if (systick_get_time_since_timestamp(vbat_charge_timestamp) >= VBAT_CHARGING_TIME_MS) {
            stop_charge();
            // vbat_charge_timestamp переиспользуем как отметку начала релаксации
            vbat_charge_timestamp = systick_get_system_time_ms();
            vbat_state = VBAT_STATE_RELAX;
        }
        break;

    case VBAT_STATE_RELAX:
        // Ждём стечения поверхностного заряда, затем измеряем для фиксации
        // результата зарядки (заряд по итогам этого измерения не запускаем).
        if (systick_get_time_since_timestamp(vbat_charge_timestamp) < VBAT_RELAX_TIME_MS) {
            break;
        }
        vbat_meas_after_charge = true;
        start_measurement();    // если АЦП не готов, останемся в RELAX и попробуем на след. вызове
        break;
    }

    update_regmap();
}

void mcu_vbat_trigger_measurement(void)
{
    // Принудительно прерываем текущее состояние (в т.ч. зарядку) и запускаем
    // измерение немедленно: переходим в IDLE с уже истекшим периодом,
    // ближайший do_periodic_work запустит измерение.
    stop_charge();
    vbat_meas_timestamp = systick_get_system_time_ms() - VBAT_ADC_MEAS_PERIOD_MS;
    vbat_state = VBAT_STATE_IDLE;
}

void mcu_vbat_restart_charging(void)
{
    start_charge();
    vbat_charge_timestamp = systick_get_system_time_ms();
    vbat_state = VBAT_STATE_CHARGING;
}
