#include "mcu-vbat.h"
#include "config.h"
#include "wbmcu_system.h"
#include "adc.h"
#include "systick.h"
#include "rtc.h"
#include "regmap-int.h"
#include <stdbool.h>
#include <string.h>

/**
 * Заряд RTC-батарейки (MS621T) с автокалибровкой.
 *
 * Калибровка: включаем заряд и ждём, пока напряжение узла VBAT перестанет расти.
 * На плато ток заряда ~0 и узел равен VDD, т.е. "3,3 В". Показание в этот момент - x.
 * Цель удержания по пропорции: y = x * 3000 / 3300 (это "3,0 В" в единицах этого МК).
 *
 * Удержание: заряд выключен, раз в сутки замер ячейки после паузы.
 * Если ниже y - заряжаем порциями по 15 мин с замером после каждой, пока не дойдёт до y.
 *
 * Плато = рост за 12 ч <= 2 мВ И зазор (VDDA - U, питание МК минус узел) <= 60 мВ:
 * заряженная ячейка стоит у питания, неисправная может остановиться далеко от него.
 *
 * Ошибки (флаги в регистре состояния):
 *  - рост остановился, но зазор > 60 мВ - ячейка не заряжается (CAL_FAIL + CELL_FAIL);
 *  - за 96 ч плато нет: зазор <= 60 мВ - ячейка почти заряжена, x = U (флаг CAL_BY_GAP);
 *    больше - ошибка (CAL_FAIL).
 *    При ошибке калибровки заряд выключаем, x не сохраняем, через сутки калибровка заново;
 *  - в проверке порции по 15 мин идут подряд, пока ячейка не дойдёт до y;
 *    каждые 12 ч реального заряда (48 полных порций) ячейку сравниваем с замером 12 ч заряда назад:
 *    выросла <= 20 мВ - не заряжается (CHARGE_FAIL + CELL_FAIL). Окно 12 ч, а не одна порция:
 *    в медленной зоне ~2,8 В порция 15 мин даёт всего 1-3 мВ, а за 12 ч - десятки мВ;
 *  - 96 ч реального заряда (384 полных порции) не довели ячейку до y: CHARGE_FAIL.
 *    При ошибке заряда заряд выключаем, через сутки обычная проверка;
 *    флаги снимаются, когда проверка дойдёт до y.
 * Пока висит ошибка, ячейку от 3,3 В держит диод D1 (~2,7-2,8 В), RTC работает.
 *
 * Каждый час калибровки в регистры 0xE9-0xEC пишутся U, VDDA, рост за 12 ч и зазор:
 * по ним видно, насколько ячейка не дозарядилась.
 *
 * x хранится в бэкап-регистре RTC и переживает сброс EC, пока жива батарейка.
 * Пока EC работает, регистр сверяется с x из памяти каждый цикл и при расхождении
 * перезаписывается (его может стереть reset_rtc, tamper, снятие защиты флеша).
 * x из регистра проверяется на разумный диапазон, иначе - калибровка.
 * Перед уходом EC в standby заряд и делитель выключаются.
 * Раз в секунду мерим питание МК (VDDA по VREFINT):
 *  - 2500-3600 мВ - АЦП исправен; вне - сбой VREFINT, отсчёт пропускаем,
 *    час без исправного отсчёта - ошибка АЦП (ADC_FAIL), заряд выкл., повтор через сутки;
 *  - ниже 3200 мВ - низкое питание (EC от WBMZ: 2,7-3,3 В, wbec.c). Заряд и калибровка на паузе
 *    без ошибки: при питании ниже ячейки ток через 1,5 кОм пошёл бы из ячейки, а плато калибровки
 *    было бы ниже настоящего x. Замер ячейки идёт и при низком питании (он через VREFINT).
 *    Флаг LOW_SUPPLY (бит 6) только показывает это состояние.
 *
 * Делитель VBAT/3 нагружает ячейку, поэтому он включён только при калибровке,
 * замере и порции заряда. В паузе и ожидании выключен, узел (0xE0) там = 0.
 */

#define VBAT_VDD_NOMINAL_MV         3300
#define VBAT_HOLD_NOMINAL_MV        3000

#define MIN_MS                      (60UL * 1000)
#define HOUR_MS                     (60UL * MIN_MS)

#define VBAT_CAL_SAMPLE_PERIOD_MS   HOUR_MS             // точка калибровки - среднее за час
#define VBAT_CAL_WINDOW_H           12                  // плато: рост за 12 ч не больше порога
#define VBAT_CAL_PLATEAU_MV         2
#define VBAT_CAL_MIN_H              24                  // раньше суток плато не ищем
#define VBAT_CAL_MAX_H              96                  // Seiko: полный заряд MS621T за 96 ч; нет плато - смотрим зазор
#define VBAT_CAL_GAP_MAX_MV         60                  // зазор VDDA - U на 96 ч; больше - ошибка (на плато уточнить на стенде)
#define VBAT_GROWTH_NONE            INT16_MIN           // рост за 12 ч ещё не посчитан

#define VBAT_CHECK_PERIOD_MS        (24 * HOUR_MS)
#define VBAT_RELAX_MS               (10 * MIN_MS)       // пауза после снятия заряда
#define VBAT_MEAS_MS                (2 * MIN_MS)        // усреднение замера ячейки
#define VBAT_CHARGE_CHUNK_MS        (15 * MIN_MS)       // за 1 ч ячейка перезаряжалась до ~3,14 В
#define VBAT_CHARGE_MAX_MS          (96 * HOUR_MS)      // 96 ч заряда (Seiko) не хватило до y - ошибка; после полного разряда нужно ~59 ч
#define VBAT_CHARGE_PROGRESS_MS     (12 * HOUR_MS)      // каждые 12 ч заряда (48 порций) проверяем, растёт ли ячейка
// считаем реальное время заряда, а не число порций: при низком питании порция обрывается раньше
#define VBAT_CHARGE_PROGRESS_MV     20                  // выросла не больше - не заряжается (у порога порция даёт +50 мВ)
#define VBAT_DIV_SETTLE_MS          5000                // после включения делителя отсчёты не берём
#define VBAT_SAMPLE_PERIOD_MS       1000                // в среднее берём 1 отсчёт в секунду, а не каждый проход цикла
#define VBAT_VDDA_FAULT_MIN_MV      2500                // VDDA по VREFINT вне 2500-3600 - сбой АЦП, отсчёт не берём
#define VBAT_VDDA_FAULT_MAX_MV      3600
#define VBAT_VDDA_OK_MIN_MV         3200                // ниже - низкое питание (WBMZ): заряд и калибровка на паузе
#define VBAT_NO_SAMPLE_MAX_MS       HOUR_MS             // час без годного отсчёта - ошибка АЦП

#define VBAT_BKP_REG_INDEX          1                   // 0 занят mcu-pwr
#define VBAT_BKP_MAGIC              0xCA1C0000UL        // rc1 писал 0xCA1B с битым x
#define VBAT_X_MIN_MV               3000                // x - плато у питания МК (~3,3 В); вне диапазона - регистр испорчен
#define VBAT_X_MAX_MV               3500

enum vbat_state {
    VBAT_STATE_CAL = 0,         // заряд включён, ищем плато
    VBAT_STATE_IDLE = 1,        // заряд выключен, ждём суточной проверки
    VBAT_STATE_RELAX = 2,       // заряд выключен, пауза перед замером
    VBAT_STATE_MEASURE = 3,     // заряд выключен, усредняем ячейку
    VBAT_STATE_CHARGE = 4,      // порция заряда 15 мин
};

enum vbat_cmd {
    VBAT_CMD_NONE = 0,
    VBAT_CMD_START_CAL = 1,     // начать калибровку заново
    VBAT_CMD_CHECK_NOW = 2,     // сделать суточную проверку сейчас
    VBAT_CMD_FORGET_CAL = 3,    // стереть x
};

#define VBAT_FLAG_CALIBRATED        (1 << 0)
#define VBAT_FLAG_CAL_FAIL          (1 << 1)    // ошибка калибровки: x не получен
#define VBAT_FLAG_CHARGE_FAIL       (1 << 2)    // ошибка заряда в проверке (не растёт или 96 ч не хватило)
#define VBAT_FLAG_CAL_BY_GAP        (1 << 3)    // плато за 96 ч не найдено, но зазор <= 60 мВ: x = U
#define VBAT_FLAG_ADC_FAIL          (1 << 5)    // ошибка: час нет исправного отсчёта (VDDA вне 2500-3600 мВ)
#define VBAT_FLAG_LOW_SUPPLY        (1 << 6)    // не ошибка: питание МК < 3200 мВ, заряд и калибровка на паузе
#define VBAT_FLAG_CELL_FAIL         (1 << 4)    // ошибка: ячейка не заряжается (калибровка: рост встал далеко от питания; проверка: за 48 порций <= 20 мВ)

static struct {
    enum vbat_state state;
    systime_t state_ts;
    systime_t last_check_ts;

    // текущее усреднение
    int64_t acc_mv;             // за час набегает больше 2^31 мВ
    int64_t acc_vdda_mv;
    uint32_t acc_n;             // не больше 3600 за час (1 отсчёт в секунду)
    systime_t acc_ts;
    systime_t sample_ts;        // когда взят последний отсчёт в среднее
    systime_t good_ts;          // когда был последний исправный отсчёт VDDA (для ошибки АЦП)
    bool supply_ok;             // питание МК >= 3200 мВ: можно заряжать и калибровать

    // калибровка
    uint16_t cal_hours;
    uint16_t cal_points[VBAT_CAL_WINDOW_H + 1];
    uint8_t cal_points_n;
    uint16_t cal_u_mv;          // последний часовой U
    uint16_t cal_vdda_mv;       // VDDA за тот же час
    int16_t cal_growth_mv;      // U - U 12 ч назад

    uint16_t node_mv;
    uint16_t cell_mv;
    uint16_t x_mv;
    uint16_t y_mv;
    uint16_t flags;
    uint32_t charged_ms;        // сколько реально заряжали в этой проверке (до 96 ч = 345,6 млн мс)
    uint32_t ref_charged_ms;    // charged_ms в момент запомненного замера
    uint16_t cell_ref_mv;       // замер 12 ч заряда назад: с ним сравниваем рост
    bool ref_valid;             // запомненный замер есть (в начале проверки - нет)

    // что последний раз записано в регмап (пишем только при изменении)
    struct REGMAP_VBAT_STATUS regmap_status;
    struct REGMAP_VBAT_CAL regmap_cal;

    bool div_on;
    systime_t div_ts;
} vbat;

static void divider_set(bool on)
{
    if (on && !vbat.div_on) {
        ADC->CCR |= ADC_CCR_VBATEN;
        vbat.div_ts = systick_get_system_time_ms();
    } else if (!on) {
        ADC->CCR &= ~ADC_CCR_VBATEN;
    }
    vbat.div_on = on;
}

static inline void charge_on(void)
{
    PWR->CR4 |= PWR_CR4_VBE;
}

static inline void charge_off(void)
{
    PWR->CR4 &= ~PWR_CR4_VBE;
}

static inline bool is_charging(void)
{
    return (PWR->CR4 & PWR_CR4_VBE) != 0;
}

static void set_state(enum vbat_state s)
{
    vbat.state = s;
    vbat.state_ts = systick_get_system_time_ms();
    divider_set((s == VBAT_STATE_CAL) || (s == VBAT_STATE_MEASURE) || (s == VBAT_STATE_CHARGE));
    vbat.acc_mv = 0;
    vbat.acc_vdda_mv = 0;
    vbat.acc_n = 0;
    vbat.acc_ts = vbat.state_ts;
}

static void set_x(uint16_t x_mv)
{
    vbat.x_mv = x_mv;
    vbat.y_mv = (uint32_t)x_mv * VBAT_HOLD_NOMINAL_MV / VBAT_VDD_NOMINAL_MV;
    vbat.flags |= VBAT_FLAG_CALIBRATED;
    vbat.flags &= ~(VBAT_FLAG_CAL_FAIL | VBAT_FLAG_CELL_FAIL | VBAT_FLAG_ADC_FAIL);
    rtc_save_to_tamper_reg(VBAT_BKP_REG_INDEX, VBAT_BKP_MAGIC | x_mv);
}

static void start_calibration(void)
{
    vbat.x_mv = 0;
    vbat.y_mv = 0;
    vbat.flags &= VBAT_FLAG_CAL_FAIL | VBAT_FLAG_CELL_FAIL | VBAT_FLAG_ADC_FAIL;   // ошибка прошлой калибровки видна, пока эта не найдёт плато
    vbat.cal_hours = 0;
    vbat.cal_points_n = 0;
    vbat.cal_u_mv = 0;
    vbat.cal_vdda_mv = 0;
    vbat.cal_growth_mv = VBAT_GROWTH_NONE;
    rtc_save_to_tamper_reg(VBAT_BKP_REG_INDEX, 0);
    if (vbat.supply_ok) {
        charge_on();            // при низком питании заряд включит состояние CAL, когда питание вернётся
    }
    set_state(VBAT_STATE_CAL);
}

static void start_check(void)
{
    charge_off();
    vbat.charged_ms = 0;
    vbat.ref_charged_ms = 0;
    vbat.ref_valid = false;
    vbat.last_check_ts = systick_get_system_time_ms();
    set_state(VBAT_STATE_RELAX);
}

static void update_regmap(void)
{
    struct REGMAP_VBAT_STATUS r = {
        .node_mv = vbat.node_mv,
        .state = vbat.state,
        .x_mv = vbat.x_mv,
        .y_mv = vbat.y_mv,
        .cell_mv = vbat.cell_mv,
        .flags = vbat.flags | (vbat.supply_ok ? 0 : VBAT_FLAG_LOW_SUPPLY) | (is_charging() ? (1 << 15) : 0),
        .state_min = systick_get_time_since_timestamp(vbat.state_ts) / MIN_MS,
        .cal_hours = vbat.cal_hours,
    };
    if (memcmp(&r, &vbat.regmap_status, sizeof(r)) != 0) {
        if (regmap_set_region_data(REGMAP_REGION_VBAT_STATUS, &r, sizeof(r))) {
            vbat.regmap_status = r;     // не записалось (регмап занят) - повторим в следующем цикле
        }
    }

    struct REGMAP_VBAT_CAL c = {
        .u_mv = vbat.cal_u_mv,
        .vdda_mv = vbat.cal_vdda_mv,
        .growth_mv = vbat.cal_growth_mv,
        .gap_mv = vbat.cal_u_mv ? (int16_t)((int32_t)vbat.cal_vdda_mv - vbat.cal_u_mv) : 0,
    };
    if (memcmp(&c, &vbat.regmap_cal, sizeof(c)) != 0) {
        if (regmap_set_region_data(REGMAP_REGION_VBAT_CAL, &c, sizeof(c))) {
            vbat.regmap_cal = c;
        }
    }
}

// Регистр с x могли стереть, пока EC работает: восстанавливаем из памяти
static void keep_bkp_reg(void)
{
    if (!(vbat.flags & VBAT_FLAG_CALIBRATED) || (vbat.x_mv == 0)) {
        return;     // x нет (калибровка или её ошибка) - регистр и должен быть пуст
    }
    uint32_t want = VBAT_BKP_MAGIC | vbat.x_mv;
    if (rtc_get_tamper_reg(VBAT_BKP_REG_INDEX) != want) {
        rtc_save_to_tamper_reg(VBAT_BKP_REG_INDEX, want);
    }
}

static void handle_cmd(void)
{
    struct REGMAP_VBAT_CTRL c = {};
    if (!regmap_get_data_if_region_changed(REGMAP_REGION_VBAT_CTRL, &c, sizeof(c))) {
        return;
    }
    switch (c.cmd) {
    case VBAT_CMD_START_CAL:
        start_calibration();
        break;
    case VBAT_CMD_CHECK_NOW:
        if (vbat.flags & VBAT_FLAG_CALIBRATED) {
            start_check();
        }
        break;
    case VBAT_CMD_FORGET_CAL:
        // стереть x и все флаги ошибок; без x делать нечего, кроме калибровки
        vbat.flags = 0;
        start_calibration();
        break;
    default:
        break;
    }
    c.cmd = VBAT_CMD_NONE;
    regmap_set_region_data(REGMAP_REGION_VBAT_CTRL, &c, sizeof(c));
}

// Ошибка заряда в проверке: заряд выкл., следующая проверка через сутки от этого момента
static void charge_fail(uint16_t extra_flag)
{
    charge_off();
    vbat.flags |= VBAT_FLAG_CHARGE_FAIL | extra_flag;
    vbat.last_check_ts = systick_get_system_time_ms();
    set_state(VBAT_STATE_IDLE);
}

// Ошибка калибровки: заряд выкл., x не сохраняем, через сутки калибровка заново
static void cal_fail(uint16_t extra_flag)
{
    charge_off();
    vbat.flags |= VBAT_FLAG_CAL_FAIL | extra_flag;
    vbat.last_check_ts = systick_get_system_time_ms();
    set_state(VBAT_STATE_IDLE);
}

// Калибровка: раз в час добавляем среднее за час и проверяем плато
static void cal_hour_point(uint16_t mv, uint16_t vdda_mv)
{
    vbat.cal_u_mv = mv;
    vbat.cal_vdda_mv = vdda_mv;
    if (vbat.cal_points_n < VBAT_CAL_WINDOW_H + 1) {
        vbat.cal_points[vbat.cal_points_n++] = mv;
    } else {
        for (unsigned i = 0; i < VBAT_CAL_WINDOW_H; i++) {
            vbat.cal_points[i] = vbat.cal_points[i + 1];
        }
        vbat.cal_points[VBAT_CAL_WINDOW_H] = mv;
    }
    vbat.cal_hours++;

    if (vbat.cal_points_n == VBAT_CAL_WINDOW_H + 1) {
        vbat.cal_growth_mv = (int16_t)((int32_t)vbat.cal_points[VBAT_CAL_WINDOW_H] - vbat.cal_points[0]);
    }
    // заряженная ячейка стоит у питания МК; далеко от него - не заряжена
    bool near_vdda = ((int32_t)vdda_mv - mv) <= VBAT_CAL_GAP_MAX_MV;
    bool flat = (vbat.cal_hours >= VBAT_CAL_MIN_H) &&
                (vbat.cal_growth_mv != VBAT_GROWTH_NONE) &&
                (vbat.cal_growth_mv <= VBAT_CAL_PLATEAU_MV);

    if (flat && near_vdda) {
        set_x(mv);                              // плато
        start_check();
    } else if (flat) {
        cal_fail(VBAT_FLAG_CELL_FAIL);          // рост остановился далеко от питания
    } else if (vbat.cal_hours >= VBAT_CAL_MAX_H) {
        if (near_vdda) {
            set_x(mv);                          // плато не дождались, но ячейка почти заряжена
            vbat.flags |= VBAT_FLAG_CAL_BY_GAP;
            start_check();
        } else {
            cal_fail(0);                        // за 96 ч не дошла до питания
        }
    }
}

void mcu_vbat_init(void)
{
    PWR->CR4 |= PWR_CR4_VBRS;   // 1,5 кОм: последовательно есть ещё R11 3 кОм
    uint32_t bkp = rtc_get_tamper_reg(VBAT_BKP_REG_INDEX);
    uint16_t x = bkp & 0xFFFF;
    if (((bkp & 0xFFFF0000UL) == VBAT_BKP_MAGIC) && (x >= VBAT_X_MIN_MV) && (x <= VBAT_X_MAX_MV)) {
        set_x(x);
        start_check();
    } else {
        start_calibration();
    }
}

void mcu_vbat_check_do_periodic_work(void)
{
    if (!adc_get_ready()) {
        return;
    }

    handle_cmd();

    bool div_ready = vbat.div_on &&
                     (systick_get_time_since_timestamp(vbat.div_ts) >= VBAT_DIV_SETTLE_MS);
    if (!vbat.div_on) {
        vbat.node_mv = 0;
    } else if (!div_ready) {
        // фильтр держим на свежем отсчёте, пока делитель устанавливается
        adc_reset_lowpass(ADC_CHANNEL_ADC_INT_VBAT);
        vbat.node_mv = 0;
    } else {
        vbat.node_mv = adc_get_ch_mv(ADC_CHANNEL_ADC_INT_VBAT);
    }

    if (systick_get_time_since_timestamp(vbat.sample_ts) >= VBAT_SAMPLE_PERIOD_MS) {
        vbat.sample_ts = systick_get_system_time_ms();
        int32_t vdda = adc_get_vdda_mv();
        // узел тоже пересчитывается через VREFINT: при сбое VREFINT неверны оба - отсчёт пропускаем
        bool vref_ok = (vdda >= VBAT_VDDA_FAULT_MIN_MV) && (vdda <= VBAT_VDDA_FAULT_MAX_MV);
        vbat.supply_ok = vref_ok && (vdda >= VBAT_VDDA_OK_MIN_MV);
        if (vref_ok) {
            vbat.good_ts = vbat.sample_ts;
            if (div_ready) {
                vbat.acc_mv += vbat.node_mv;
                vbat.acc_vdda_mv += vdda;
                vbat.acc_n++;
            }
        }
    }
    if (systick_get_time_since_timestamp(vbat.good_ts) >= VBAT_NO_SAMPLE_MAX_MS) {
        // АЦП час не даёт исправных отсчётов: решения принимать не по чему - ошибка, повтор через сутки
        vbat.good_ts = systick_get_system_time_ms();    // следующая ошибка не раньше чем через час
        if (vbat.state == VBAT_STATE_CAL) {
            cal_fail(VBAT_FLAG_ADC_FAIL);
        } else {
            charge_fail(VBAT_FLAG_ADC_FAIL);
        }
    }

    systime_t in_state = systick_get_time_since_timestamp(vbat.state_ts);

    switch (vbat.state) {
    case VBAT_STATE_CAL:
        if (!vbat.supply_ok) {
            // низкое питание: заряд выкл., неполный час не считаем
            charge_off();
            vbat.acc_mv = 0;
            vbat.acc_vdda_mv = 0;
            vbat.acc_n = 0;
            vbat.acc_ts = systick_get_system_time_ms();
            break;
        }
        if (!is_charging()) {
            // питание вернулось: продолжаем с того же часа, час начинаем заново
            charge_on();
            vbat.acc_mv = 0;
            vbat.acc_vdda_mv = 0;
            vbat.acc_n = 0;
            vbat.acc_ts = systick_get_system_time_ms();
            break;
        }
        if ((systick_get_time_since_timestamp(vbat.acc_ts) >= VBAT_CAL_SAMPLE_PERIOD_MS) && (vbat.acc_n > 0)) {
            uint16_t avg = vbat.acc_mv / (int64_t)vbat.acc_n;
            uint16_t vdda = vbat.acc_vdda_mv / (int64_t)vbat.acc_n;
            vbat.acc_mv = 0;
            vbat.acc_vdda_mv = 0;
            vbat.acc_n = 0;
            vbat.acc_ts = systick_get_system_time_ms();
            cal_hour_point(avg, vdda);
        }
        break;

    case VBAT_STATE_IDLE:
        if (systick_get_time_since_timestamp(vbat.last_check_ts) >= VBAT_CHECK_PERIOD_MS) {
            if (vbat.flags & VBAT_FLAG_CALIBRATED) {
                start_check();
            } else if (vbat.flags & VBAT_FLAG_CAL_FAIL) {
                start_calibration();
            }
        }
        break;

    case VBAT_STATE_RELAX:
        if (in_state >= VBAT_RELAX_MS) {
            set_state(VBAT_STATE_MEASURE);
        }
        break;

    case VBAT_STATE_MEASURE:
        if ((in_state >= VBAT_MEAS_MS) && (vbat.acc_n > 0)) {
            vbat.cell_mv = vbat.acc_mv / (int64_t)vbat.acc_n;
            if (!vbat.ref_valid) {
                // первый замер проверки - точка отсчёта роста
                vbat.cell_ref_mv = vbat.cell_mv;
                vbat.ref_charged_ms = vbat.charged_ms;
                vbat.ref_valid = true;
            }
            // прошли очередные 12 ч реального заряда: проверяем рост и сдвигаем точку отсчёта
            bool progress_point = (vbat.charged_ms - vbat.ref_charged_ms) >= VBAT_CHARGE_PROGRESS_MS;
            bool no_growth = progress_point &&
                             ((int32_t)vbat.cell_mv - vbat.cell_ref_mv <= VBAT_CHARGE_PROGRESS_MV);
            if (progress_point && !no_growth) {
                vbat.cell_ref_mv = vbat.cell_mv;
                vbat.ref_charged_ms = vbat.charged_ms;
            }

            if (vbat.cell_mv >= vbat.y_mv) {
                vbat.flags &= ~(VBAT_FLAG_CHARGE_FAIL | VBAT_FLAG_CELL_FAIL | VBAT_FLAG_ADC_FAIL);
                set_state(VBAT_STATE_IDLE);
            } else if (no_growth) {
                // за 12 ч заряда ячейка не выросла: дальше заряжать бессмысленно
                charge_fail(VBAT_FLAG_CELL_FAIL);
            } else if (vbat.charged_ms >= VBAT_CHARGE_MAX_MS) {
                // растёт, но 96 ч заряда не хватило
                charge_fail(0);
            } else if (!vbat.supply_ok) {
                // низкое питание: заряжать нельзя, замер повторим после паузы
                set_state(VBAT_STATE_RELAX);
            } else {
                charge_on();
                set_state(VBAT_STATE_CHARGE);
            }
        }
        break;

    case VBAT_STATE_CHARGE:
        if ((in_state >= VBAT_CHARGE_CHUNK_MS) || !vbat.supply_ok) {
            // порция кончилась или питание упало (тогда порция короче - в счёт идёт реальное время)
            vbat.charged_ms += in_state;
            charge_off();
            set_state(VBAT_STATE_RELAX);
        }
        break;
    }

    keep_bkp_reg();
    update_regmap();
}

// Перед standby: заряд и делитель выключить, чтобы ячейка не заряжалась без контроля
void mcu_vbat_prepare_standby(void)
{
    charge_off();
    divider_set(false);
}
