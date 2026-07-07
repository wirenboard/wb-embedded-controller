#include "mcu-vbat.h"
#include "config.h"
#include "wbmcu_system.h"
#include "adc.h"
#include "systick.h"
#include "fix16.h"
#include "regmap-int.h"
#include "temperature-control.h"
#include <stdbool.h>

/**
 * Обслуживание батарейки RTC (MS621FE), подключённой к пину VBAT через
 * резистор 3 кОм. Заряд выполняется встроенной в STM32 цепочкой
 * (PWR_CR4_VBE): пин VBAT подключается к VDD через внутренний резистор
 * 1.5 кОм (PWR_CR4_VBRS=1).
 *
 * Особенность схемы: узел VBAT дополнительно подпитывается от V_EC через
 * два последовательных диода (BAV199), поэтому при поданном питании
 * напряжение узла не опускается ниже ~2.6-2.7 В (зависит от температуры).
 * Из-за этого разряженная батарейка неотличима от подсевшей по абсолютному
 * напряжению: рабочее плато MS621FE (2.5-2.75 В) лежит на уровне диодного
 * пола, а полностью заряженная батарейка релаксирует к ~3.0 В — окно
 * различимых значений меньше суммарной ошибки измерения во всём диапазоне
 * температур.
 *
 * Поэтому состояние батарейки определяется не по абсолютному напряжению,
 * а по току принятия заряда (ΔV-тест). Внутренний резистор 1.5 кОм
 * работает как шунт: при включённом VBE пин VBAT показывает
 * VDD - I*1.5к. Разница между напряжением узла под зарядом (V2) и без
 * заряда (V1) пропорциональна току заряда и не зависит ни от диодного
 * пола (под зарядом диоды заперты), ни от допуска V_EC, ни от ошибки
 * усиления канала АЦП (сокращается в разности):
 *   ΔV = V2 - V1 ≈ I_заряда * 3.08 кОм (R11 3к + 80 Ом внутр. батарейки)
 *
 * Ориентиры (VDD = 3.3 В, суммарное сопротивление тракта 4.58 кОм):
 *   - полная батарейка (OCV ~3.05 В):  I ≈ 55 мкА,  ΔV ≈ 170 мВ
 *   - батарейка на плато (OCV ~2.6 В): I ≈ 150 мкА, ΔV ≈ 400 мВ
 *   - батарейка отсутствует:           I = 0,       ΔV ≈ 600+ мВ
 *     (узел без батарейки поднимается от диодного пола до VDD)
 *
 * Алгоритм:
 *   1. Раз в сутки — ΔV-тест (заряд на время теста включается на ~30 с,
 *      доза ничтожна: ~2.5 мкА*ч).
 *   2. Если ΔV показывает разряженную батарейку — полный цикл заряда:
 *      бёрсты по 55 мин с паузами по 5 мин, в конце паузы измеряется OCV.
 *      Заряд останавливается при OCV >= 3.05 В (стандартный заряд Seiko:
 *      CC/CV 3.1 В) или по суммарному времени 96 ч (страховка, стандарт
 *      Seiko: 0.1 мА / 3.1 В / 96 ч).
 *   3. Заряд разрешён только в допустимом диапазоне температур платы
 *      (рабочий диапазон MS621FE: -20..+60 °C). При выходе температуры
 *      за пределы заряд приостанавливается и продолжается после
 *      возвращения в диапазон (с гистерезисом).
 */

// Период ΔV-теста
#define VBAT_DELTAV_TEST_PERIOD_MS      (1*24*3600*1000)
// Время на стабилизацию делителя VBAT и набор свежего значения в DMA-буфере
#define VBAT_MEAS_STABILIZE_MS          30
// Выдержка после включения VBE перед измерением V2:
// ток принятия заряда стабилизируется за десятки секунд
#define VBAT_DELTAV_SETTLE_MS           (30*1000)

// ΔV выше порога — батарейка разряжена, нужен полный цикл заряда
// (соответствует OCV ниже ~2.85 В)
#define VBAT_DELTAV_CHARGE_THRESHOLD_MV 300
// ΔV выше порога — батарейка отсутствует или оборвана
#define VBAT_DELTAV_NO_BATTERY_MV       550

// Полный цикл заряда: бёрсты с паузами
#define VBAT_CHARGE_BURST_MS            (55*60*1000)
#define VBAT_CHARGE_PAUSE_MS            (5*60*1000)
// Отсечка по OCV, измеренному в конце паузы
#define VBAT_CHARGE_STOP_OCV_MV         3050
// Страховочное ограничение суммарного времени заряда за один цикл
#define VBAT_CHARGE_TIMEOUT_MS          (96*3600*1000)

// Диапазон температур платы, в котором разрешён заряд
#define VBAT_CHARGE_TEMP_MIN_C_X100     (-1500)
#define VBAT_CHARGE_TEMP_MAX_C_X100     (5500)
// Гистерезис на возобновление заряда после выхода за пределы
#define VBAT_CHARGE_TEMP_HYST_C_X100    (500)

enum vbat_state {
    VBAT_STATE_IDLE,            // ждём очередного ΔV-теста
    VBAT_STATE_TEST_V1,         // делитель VBAT включён, ждём стабилизации, меряем V1
    VBAT_STATE_TEST_V2,         // VBE включён, ждём стабилизации тока, меряем V2
    VBAT_STATE_CHARGE_BURST,    // идёт бёрст заряда, делитель выключен
    VBAT_STATE_CHARGE_PAUSE,    // пауза заряда, в конце меряем OCV
    VBAT_STATE_TEMP_WAIT,       // заряд приостановлен из-за температуры
};

struct vbat_ctx {
    enum vbat_state state;
    systime_t state_timestamp;
    int32_t vbat_mv;            // последнее измеренное напряжение без заряда (OCV)
    int32_t deltav_mv;          // результат последнего ΔV-теста
    int32_t test_v1_mv;
    bool battery_absent;
    uint32_t charge_time_total_ms;  // суммарное время заряда в текущем цикле
};

static struct vbat_ctx vbat_ctx;

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

static inline bool is_temperature_in_charge_range(void)
{
    int16_t t = temperature_control_get_temperature_c_x100();
    return ((t >= VBAT_CHARGE_TEMP_MIN_C_X100) &&
            (t <= VBAT_CHARGE_TEMP_MAX_C_X100));
}

static inline bool is_temperature_in_charge_range_with_hyst(void)
{
    int16_t t = temperature_control_get_temperature_c_x100();
    return ((t >= VBAT_CHARGE_TEMP_MIN_C_X100 + VBAT_CHARGE_TEMP_HYST_C_X100) &&
            (t <= VBAT_CHARGE_TEMP_MAX_C_X100 - VBAT_CHARGE_TEMP_HYST_C_X100));
}

static inline void set_state(enum vbat_state s)
{
    vbat_ctx.state = s;
    vbat_ctx.state_timestamp = systick_get_system_time_ms();
}

static inline void update_regmap(void)
{
    struct REGMAP_VBAT_STATUS r;
    r.voltage_mv = vbat_ctx.vbat_mv;
    r.delta_mv = vbat_ctx.deltav_mv;
    r.is_charging = is_charging() ? 1 : 0;
    r.battery_absent = vbat_ctx.battery_absent ? 1 : 0;
    regmap_set_region_data(REGMAP_REGION_VBAT_STATUS, &r, sizeof(r));
}

void mcu_vbat_init(void)
{
    PWR->CR4 |= PWR_CR4_VBRS;   // для зарядки выбираем резистор 1.5 кОм, т.к. последовательно есть еще аппаратный резистор 3кОм
    stop_charge();
    vbat_ctx.state = VBAT_STATE_IDLE;
    vbat_ctx.battery_absent = false;
    // Установим timestamp так, чтобы первый ΔV-тест произошёл сразу при включении:
    // если контроллер долго лежал без питания, батарейка ещё не подтянута
    // диодами и тест даст наиболее честный результат
    vbat_ctx.state_timestamp = systick_get_system_time_ms() - VBAT_DELTAV_TEST_PERIOD_MS;
}

void mcu_vbat_check_do_periodic_work(void)
{
    switch (vbat_ctx.state) {
    case VBAT_STATE_IDLE:
        if (systick_get_time_since_timestamp(vbat_ctx.state_timestamp) < VBAT_DELTAV_TEST_PERIOD_MS) {
            break;
        }
        if (!adc_get_ready()) {
            break;
        }
        // ΔV-тест включает заряд на ~30 с, поэтому тоже гейтится температурой
        if (!is_temperature_in_charge_range()) {
            break;
        }
        adc_int_vbat_divider_enable(true);
        set_state(VBAT_STATE_TEST_V1);
        break;

    case VBAT_STATE_TEST_V1:
        if (systick_get_time_since_timestamp(vbat_ctx.state_timestamp) < VBAT_MEAS_STABILIZE_MS) {
            break;
        }
        vbat_ctx.test_v1_mv = adc_get_ch_mv(ADC_CHANNEL_ADC_INT_VBAT);
        vbat_ctx.vbat_mv = vbat_ctx.test_v1_mv;
        start_charge();
        set_state(VBAT_STATE_TEST_V2);
        break;

    case VBAT_STATE_TEST_V2: {
        if (systick_get_time_since_timestamp(vbat_ctx.state_timestamp) < VBAT_DELTAV_SETTLE_MS) {
            break;
        }
        int32_t v2_mv = adc_get_ch_mv(ADC_CHANNEL_ADC_INT_VBAT);
        int32_t delta_mv = v2_mv - vbat_ctx.test_v1_mv;
        if (delta_mv < 0) {
            delta_mv = 0;
        }
        vbat_ctx.deltav_mv = delta_mv;

        if (delta_mv >= VBAT_DELTAV_NO_BATTERY_MV) {
            // Ток заряда отсутствует - батарейки нет
            vbat_ctx.battery_absent = true;
            stop_charge();
            adc_int_vbat_divider_enable(false);
            set_state(VBAT_STATE_IDLE);
        } else if (delta_mv >= VBAT_DELTAV_CHARGE_THRESHOLD_MV) {
            // Большой ток принятия заряда - батарейка разряжена.
            // VBE уже включён, продолжаем зарядку бёрстом
            vbat_ctx.battery_absent = false;
            vbat_ctx.charge_time_total_ms = 0;
            adc_int_vbat_divider_enable(false);
            set_state(VBAT_STATE_CHARGE_BURST);
        } else {
            // Батарейка заряжена
            vbat_ctx.battery_absent = false;
            stop_charge();
            adc_int_vbat_divider_enable(false);
            set_state(VBAT_STATE_IDLE);
        }
        break;
    }

    case VBAT_STATE_CHARGE_BURST:
        if (!is_temperature_in_charge_range()) {
            stop_charge();
            set_state(VBAT_STATE_TEMP_WAIT);
            break;
        }
        if (systick_get_time_since_timestamp(vbat_ctx.state_timestamp) < VBAT_CHARGE_BURST_MS) {
            break;
        }
        vbat_ctx.charge_time_total_ms += VBAT_CHARGE_BURST_MS;
        stop_charge();
        // Делитель включаем на всю паузу: измерение в конце паузы,
        // после релаксации поверхностного заряда
        adc_int_vbat_divider_enable(true);
        set_state(VBAT_STATE_CHARGE_PAUSE);
        break;

    case VBAT_STATE_CHARGE_PAUSE: {
        if (systick_get_time_since_timestamp(vbat_ctx.state_timestamp) < VBAT_CHARGE_PAUSE_MS) {
            break;
        }
        int32_t ocv_mv = adc_get_ch_mv(ADC_CHANNEL_ADC_INT_VBAT);
        vbat_ctx.vbat_mv = ocv_mv;
        adc_int_vbat_divider_enable(false);

        if ((ocv_mv >= VBAT_CHARGE_STOP_OCV_MV) ||
            (vbat_ctx.charge_time_total_ms >= VBAT_CHARGE_TIMEOUT_MS))
        {
            // Заряд завершён, следующий ΔV-тест через сутки
            set_state(VBAT_STATE_IDLE);
        } else {
            start_charge();
            set_state(VBAT_STATE_CHARGE_BURST);
        }
        break;
    }

    case VBAT_STATE_TEMP_WAIT:
        if (is_temperature_in_charge_range_with_hyst()) {
            start_charge();
            set_state(VBAT_STATE_CHARGE_BURST);
        }
        break;
    }

    update_regmap();
}

void mcu_vbat_trigger_measurement(void)
{
    // Принудительно прерываем текущее состояние (в т.ч. зарядку) и запускаем
    // ΔV-тест немедленно: переходим в IDLE с уже истекшим периодом,
    // ближайший do_periodic_work запустит тест.
    // Если батарейка разряжена, тест сам перезапустит зарядку.
    stop_charge();
    adc_int_vbat_divider_enable(false);
    vbat_ctx.state_timestamp = systick_get_system_time_ms() - VBAT_DELTAV_TEST_PERIOD_MS;
    vbat_ctx.state = VBAT_STATE_IDLE;
}

void mcu_vbat_restart_charging(void)
{
    // Принудительно запускаем полный цикл заряда без ΔV-теста
    vbat_ctx.charge_time_total_ms = 0;
    adc_int_vbat_divider_enable(false);
    start_charge();
    set_state(VBAT_STATE_CHARGE_BURST);
}

void mcu_vbat_stop_charging(void)
{
    // Останавливает зарядку перед уходом в standby: битовое поле VBE
    // сохраняет своё состояние в standby, и без этого заряд продолжался бы
    // без контроля всё время сна
    stop_charge();
    adc_int_vbat_divider_enable(false);
    vbat_ctx.state = VBAT_STATE_IDLE;
    vbat_ctx.state_timestamp = systick_get_system_time_ms();
}
