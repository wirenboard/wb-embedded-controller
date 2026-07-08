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
 * резистор R11 = 3 кОм. Заряд выполняется встроенной в STM32 цепочкой
 * (PWR_CR4_VBE): пин VBAT подключается к VDD через внутренний резистор
 * 1.5 кОм (PWR_CR4_VBRS=1).
 *
 * Особенность схемы: узел VBAT дополнительно подпитывается от V_EC через
 * диод (половина сборки BAV199), поэтому при поданном питании напряжение
 * узла не опускается ниже ~2.7-3.0 В: под активной подпиткой (десятки
 * мкА) прямое падение диода ~0.55-0.6 В и узел стоит около 2.7 В, а по
 * мере насыщения ячейки ток спадает до наноампер и равновесный пол
 * поднимается к 2.9-3.0 В — практически к OCV полной батарейки. Рабочее
 * плато разряда MS621FE (2.5-2.75 В) лежит под этим полом, поэтому
 * показание узла отражает историю подпитки, а не состояние ячейки.
 * Порог по абсолютному напряжению принципиально не работает.
 *
 * Поэтому состояние батарейки определяется по току принятия заряда,
 * который измеряется через скачок напряжения узла при снятии VBE:
 *   V2  — узел под зарядом:                V2 = OCV + I*(R11 + Rбат)
 *   V2' — узел через 60 мс после VBE off:  V2' = OCV
 *   (химическое состояние ячейки за 60 мс не меняется, омические
 *   падения исчезают мгновенно)
 *   I = (V2 - V2') / (R11 + Rбат) = (V2 - V2') / 3.08 кОм
 *
 * Шунтом служит внешний резистор R11 (плюс внутреннее сопротивление
 * ячейки). Оба отсчёта берутся одним каналом АЦП с интервалом 60 мс,
 * поэтому ошибка усиления тракта (мост VBAT/3 имеет допуск ±10% по
 * DS12991) входит только мультипликативно в малую разность, допуск
 * V_EC и диодный пол не участвуют вовсе. Единственный номинал в
 * пересчёте тока — R11 (±1%).
 *
 * Ориентиры (VDD = 3.3 В, суммарное сопротивление тракта 4.58 кОм),
 * подтверждены экспериментально на WB8.5:
 *   - полная батарейка (OCV ~3.1 В):   I ~ 40-55 мкА
 *   - батарейка на плато (OCV ~2.6 В): I ~ 150 мкА
 *   - батарейка отсутствует: узел под зарядом поднимается к VDD
 *     (тока нет, V2 > 3.15 В), а после снятия VBE проседает током
 *     моста VBAT/3 до диодного пола: большой кажущийся "скачок"
 *     при высоком V2
 *
 * Алгоритм:
 *   1. Раз в сутки — тест тока принятия заряда (заряд включается
 *      на 60 с, доза ~2.5 мкА*ч — ничтожна).
 *   2. I >= VBAT_CHARGE_START_UA — батарейка разряжена, полный цикл
 *      заряда: бёрсты по 55 мин с паузами по 5 мин, в конце паузы
 *      измеряется OCV. Стоп при OCV >= 3.05 В (стандартный заряд Seiko:
 *      CC/CV 3.1 В) или по суммарному времени 96 ч (страховка,
 *      стандарт Seiko: 0.1 мА / 3.1 В / 96 ч).
 *   3. Заряд разрешён только в допустимом диапазоне температур платы
 *      (рабочий диапазон MS621FE: -20..+60 °C). При выходе за пределы
 *      заряд приостанавливается, возобновление с гистерезисом.
 */

// Период теста тока принятия заряда
#define VBAT_TEST_PERIOD_MS             (1*24*3600*1000)
// Выдержка после включения VBE перед измерением V2:
// ток принятия заряда стабилизируется за десятки секунд
#define VBAT_TEST_ON_MS                 (60*1000)
// Выдержка после снятия VBE перед измерением V2':
// электрическая релаксация узла ~0.3 мс, lowpass фильтра (RC 10 мс) ~50 мс
#define VBAT_TEST_JUMP_MS               60

// Сопротивление, на котором измеряется скачок: R11 3 кОм + ~80 Ом ячейки
#define VBAT_JUMP_R_SENSE_OHM           3080
#define VBAT_JUMP_MV_TO_UA(mv)          ((int32_t)(mv) * 1000 / VBAT_JUMP_R_SENSE_OHM)

// Ток принятия заряда выше порога — батарейка разряжена, нужен заряд
// (соответствует OCV ниже ~2.85 В)
#define VBAT_CHARGE_START_UA            100
// Признак отсутствия батарейки: под зарядом узел у рельсы (тока нет),
// после снятия VBE узел проседает мостом до диодного пола -
// большой кажущийся ток при высоком V2
#define VBAT_NO_BATTERY_UA              160
#define VBAT_NO_BATTERY_V2_MV           3150

// Полный цикл заряда: бёрсты с паузами
#define VBAT_CHARGE_BURST_MS            (55*60*1000)
#define VBAT_CHARGE_PAUSE_MS            (5*60*1000)
// Отсечка по OCV, измеренному в конце паузы
#define VBAT_CHARGE_STOP_OCV_MV         3050
// Страховочное ограничение суммарного времени заряда за один цикл
#define VBAT_CHARGE_TIMEOUT_MS          (96*3600*1000)

// Время на стабилизацию делителя VBAT и набор свежего значения в DMA-буфере
#define VBAT_MEAS_STABILIZE_MS          30

// Диапазон температур платы, в котором разрешён заряд
#define VBAT_CHARGE_TEMP_MIN_C_X100     (-1500)
#define VBAT_CHARGE_TEMP_MAX_C_X100     (5500)
// Гистерезис на возобновление заряда после выхода за пределы
#define VBAT_CHARGE_TEMP_HYST_C_X100    (500)

enum vbat_state {
    VBAT_STATE_IDLE,            // ждём очередного теста
    VBAT_STATE_TEST_ON,         // VBE включён, ждём стабилизации тока, меряем V2
    VBAT_STATE_TEST_JUMP,       // VBE выключен, через 60 мс меряем V2'
    VBAT_STATE_CHARGE_BURST,    // идёт бёрст заряда, делитель выключен
    VBAT_STATE_CHARGE_PAUSE,    // пауза заряда, в конце меряем OCV
    VBAT_STATE_TEMP_WAIT,       // заряд приостановлен из-за температуры
};

struct vbat_ctx {
    enum vbat_state state;
    systime_t state_timestamp;
    int32_t vbat_mv;            // последнее измеренное напряжение без заряда (~OCV)
    int32_t test_v2_mv;         // узел под зарядом в последнем тесте
    int32_t current_ua;         // ток принятия заряда в последнем тесте
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
    struct REGMAP_VBAT_STATUS r = {};
    r.voltage_mv = vbat_ctx.vbat_mv;
    r.charge_current_ua = vbat_ctx.current_ua;
    r.is_charging = is_charging() ? 1 : 0;
    r.battery_absent = vbat_ctx.battery_absent ? 1 : 0;
    r.state = vbat_ctx.state;
    regmap_set_region_data(REGMAP_REGION_VBAT_STATUS, &r, sizeof(r));
}

void mcu_vbat_init(void)
{
    PWR->CR4 |= PWR_CR4_VBRS;   // для зарядки выбираем резистор 1.5 кОм, т.к. последовательно есть еще аппаратный резистор 3кОм
    stop_charge();
    vbat_ctx.state = VBAT_STATE_IDLE;
    vbat_ctx.battery_absent = false;
    // Установим timestamp так, чтобы первый тест произошёл сразу при включении:
    // если контроллер долго лежал без питания, батарейка разряжена сильнее всего
    vbat_ctx.state_timestamp = systick_get_system_time_ms() - VBAT_TEST_PERIOD_MS;
}

void mcu_vbat_check_do_periodic_work(void)
{
    switch (vbat_ctx.state) {
    case VBAT_STATE_IDLE:
        if (systick_get_time_since_timestamp(vbat_ctx.state_timestamp) < VBAT_TEST_PERIOD_MS) {
            break;
        }
        if (!adc_get_ready()) {
            break;
        }
        // Тест включает заряд на 60 с, поэтому тоже гейтится температурой
        if (!is_temperature_in_charge_range()) {
            break;
        }
        adc_int_vbat_divider_enable(true);
        start_charge();
        set_state(VBAT_STATE_TEST_ON);
        break;

    case VBAT_STATE_TEST_ON:
        if (systick_get_time_since_timestamp(vbat_ctx.state_timestamp) < VBAT_TEST_ON_MS) {
            break;
        }
        vbat_ctx.test_v2_mv = adc_get_ch_mv(ADC_CHANNEL_ADC_INT_VBAT);
        stop_charge();
        // Защёлкиваем lowpass, чтобы быстрее набрать значение после скачка
        adc_reset_lowpass(ADC_CHANNEL_ADC_INT_VBAT);
        set_state(VBAT_STATE_TEST_JUMP);
        break;

    case VBAT_STATE_TEST_JUMP: {
        if (systick_get_time_since_timestamp(vbat_ctx.state_timestamp) < VBAT_TEST_JUMP_MS) {
            break;
        }
        int32_t v2p_mv = adc_get_ch_mv(ADC_CHANNEL_ADC_INT_VBAT);
        int32_t jump_mv = vbat_ctx.test_v2_mv - v2p_mv;
        if (jump_mv < 0) {
            jump_mv = 0;
        }
        int32_t current_ua = VBAT_JUMP_MV_TO_UA(jump_mv);
        vbat_ctx.current_ua = current_ua;
        // V2' - терминал ячейки сразу после снятия заряда, ~OCV
        vbat_ctx.vbat_mv = v2p_mv;
        adc_int_vbat_divider_enable(false);

        if ((current_ua >= VBAT_NO_BATTERY_UA) &&
            (vbat_ctx.test_v2_mv >= VBAT_NO_BATTERY_V2_MV))
        {
            // Под зарядом узел у рельсы (тока нет), после снятия заряда
            // узел провалился до диодного пола - батарейки нет
            vbat_ctx.battery_absent = true;
            set_state(VBAT_STATE_IDLE);
        } else if (current_ua >= VBAT_CHARGE_START_UA) {
            // Большой ток принятия заряда - батарейка разряжена
            vbat_ctx.battery_absent = false;
            vbat_ctx.charge_time_total_ms = 0;
            start_charge();
            set_state(VBAT_STATE_CHARGE_BURST);
        } else {
            // Батарейка заряжена
            vbat_ctx.battery_absent = false;
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
            // Заряд завершён, следующий тест через сутки
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
    // тест немедленно: переходим в IDLE с уже истекшим периодом,
    // ближайший do_periodic_work запустит тест.
    // Если батарейка разряжена, тест сам перезапустит зарядку.
    stop_charge();
    adc_int_vbat_divider_enable(false);
    vbat_ctx.state_timestamp = systick_get_system_time_ms() - VBAT_TEST_PERIOD_MS;
    vbat_ctx.state = VBAT_STATE_IDLE;
}

void mcu_vbat_restart_charging(void)
{
    // Принудительно запускаем полный цикл заряда без теста
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
