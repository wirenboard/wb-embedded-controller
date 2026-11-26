#include "unity.h"

#include "mcu-vbat.h"
#include "adc.h"
#include "regmap-int.h"
#include "rtc.h"
#include "array_size.h"
#include "utest_adc.h"
#include "utest_regmap.h"
#include "utest_rtc.h"
#include "utest_systick.h"
#include "utest_wbmcu_system.h"
#include <stdio.h>

void utest_mcu_vbat_reset_state(void);

// Внешний контракт модуля продублирован намеренно: коды состояний (0xE1), флаги (0xE5),
// команды (0xE8) и формат TAMP_BKP1R. Если прошивка их поменяет, тест должен упасть
enum {
    ST_CAL = 0,
    ST_IDLE = 1,
    ST_RELAX = 2,
    ST_MEASURE = 3,
    ST_CHARGE = 4,
};

#define STATE_BIT(s)                ((uint32_t)1 << (s))

enum {
    CMD_START_CAL = 1,
    CMD_CHECK_NOW = 2,
    CMD_FORGET_CAL = 3,
};

#define FLAG_CALIBRATED             (1 << 0)
#define FLAG_CAL_FAIL               (1 << 1)
#define FLAG_CHARGE_FAIL            (1 << 2)
#define FLAG_CAL_BY_GAP             (1 << 3)
#define FLAG_CELL_FAIL              (1 << 4)
#define FLAG_ADC_FAIL               (1 << 5)
#define FLAG_LOW_SUPPLY             (1 << 6)
#define FLAG_CHARGING               (1 << 15)
// Флаги, которые хранит автомат; LOW_SUPPLY и CHARGING показывают текущее питание и ключ заряда
#define FLAGS_STORED_MASK           0x3F

#define BKP_INDEX                   1
#define BKP_MAGIC                   0xCA1C0000UL

#define SEC_MS                      1000UL
#define MIN_MS                      (60 * SEC_MS)
#define HOUR_MS                     (60 * MIN_MS)

#define VDDA_OK_MV                  3310    // питание МК от +5 В
#define VDDA_LOW_MV                 3100    // питание от WBMZ: ниже 3200 мВ, но VREFINT исправен
#define VDDA_FAULT_LOW_MV           2400    // ниже 2500 мВ: сбой VREFINT
#define VDDA_FAULT_HIGH_MV          3700    // выше 3600 мВ: сбой VREFINT

#define X_MV                        3273    // плато калибровки со стенда
#define Y_MV                        2975    // 3273 * 3000 / 3300

// Пауза 10 мин + замер 2 мин: решение проверки приходит с запасом за это время
#define CHECK_DECISION_TIMEOUT_MS   (15 * MIN_MS)
// Порция заряда 15 мин
#define PORTION_TIMEOUT_MS          (16 * MIN_MS)

static uint16_t cell_mv;

static void reset_all(void)
{
    utest_systick_set_time_ms(0);
    utest_adc_reset();
    utest_adc_set_vdda_mv(VDDA_OK_MV);
    utest_rtc_reset();
    utest_regmap_reset();
    utest_pwr_reset();
    utest_adc_common_reset();
    utest_mcu_vbat_reset_state();
    cell_mv = 0;
}

void setUp(void)
{
    reset_all();
}

void tearDown(void)
{
}

static void set_node_mv(uint16_t mv)
{
    utest_adc_set_ch_mv(ADC_CHANNEL_ADC_INT_VBAT, mv);
}

// Один проход основного цикла EC через секунду: модуль берёт в среднее один отсчёт в секунду
static void step(void)
{
    utest_systick_advance_time_ms(SEC_MS);
    mcu_vbat_check_do_periodic_work();
}

static void run_ms(uint32_t duration_ms)
{
    for (uint32_t t = 0; t < duration_ms; t += SEC_MS) {
        step();
    }
}

static struct REGMAP_VBAT_STATUS get_status(void)
{
    struct REGMAP_VBAT_STATUS s = {};
    utest_regmap_get_region_data(REGMAP_REGION_VBAT_STATUS, &s, sizeof(s));
    return s;
}

static struct REGMAP_VBAT_CAL get_cal(void)
{
    struct REGMAP_VBAT_CAL c = {};
    utest_regmap_get_region_data(REGMAP_REGION_VBAT_CAL, &c, sizeof(c));
    return c;
}

static uint16_t get_state(void)
{
    return get_status().state;
}

static uint16_t get_stored_flags(void)
{
    return get_status().flags & FLAGS_STORED_MASK;
}

static bool is_charge_on(void)
{
    return (PWR->CR4 & PWR_CR4_VBE) != 0;
}

static bool is_divider_on(void)
{
    return (ADC->CCR & ADC_CCR_VBATEN) != 0;
}

static void send_cmd(uint16_t cmd)
{
    struct REGMAP_VBAT_CTRL c = {.cmd = cmd};
    regmap_set_region_data(REGMAP_REGION_VBAT_CTRL, &c, sizeof(c));
    utest_regmap_mark_region_changed(REGMAP_REGION_VBAT_CTRL);
}

static uint16_t get_cmd(void)
{
    struct REGMAP_VBAT_CTRL c = {};
    utest_regmap_get_region_data(REGMAP_REGION_VBAT_CTRL, &c, sizeof(c));
    return c.cmd;
}

// Крутит цикл, пока автомат не придёт в одно из состояний маски, и возвращает это состояние
static uint16_t run_until_state(uint32_t states_mask, uint32_t timeout_ms)
{
    for (uint32_t t = 0; t <= timeout_ms; t += SEC_MS) {
        uint16_t state = get_state();
        if (states_mask & STATE_BIT(state)) {
            return state;
        }
        step();
    }
    TEST_FAIL_MESSAGE("State machine did not reach the expected state in time");
    return 0xFFFF;
}

// Старт EC с сохранённым x: модуль сразу начинает суточную проверку
static void start_with_saved_x(uint16_t x_mv)
{
    rtc_save_to_tamper_reg(BKP_INDEX, BKP_MAGIC | x_mv);
    mcu_vbat_init();
    step();
}

// Старт EC без x: калибровка, заряд включается на первом отсчёте питания МК.
// Часы калибровки отсчитываются от этого момента, поэтому run_cal_hour() совпадает с ними
static void start_calibration(void)
{
    mcu_vbat_init();
    step();
}

// Час калибровки с заданным напряжением узла
static void run_cal_hour(uint16_t node_mv)
{
    set_node_mv(node_mv);
    run_ms(HOUR_MS);
}

static void run_cal_hours(uint16_t node_mv, uint32_t hours)
{
    for (uint32_t h = 0; h < hours; h++) {
        run_cal_hour(node_mv);
    }
}

// Суточная проверка до конца, возвращает число порций заряда.
// После каждой порции (до замера) вызывает after_portion: там тест задаёт, что покажет ячейка
static uint32_t run_check_portions(void (*after_portion)(uint32_t portion))
{
    uint32_t portions = 0;
    uint32_t decision_mask = STATE_BIT(ST_CHARGE) | STATE_BIT(ST_IDLE);
    uint16_t state = run_until_state(decision_mask, CHECK_DECISION_TIMEOUT_MS);
    while (state == ST_CHARGE) {
        portions++;
        run_until_state(STATE_BIT(ST_RELAX), PORTION_TIMEOUT_MS);
        if (after_portion) {
            after_portion(portions);
        }
        state = run_until_state(decision_mask, CHECK_DECISION_TIMEOUT_MS);
    }
    return portions;
}

// ==================== Старт EC ====================

// Сценарий: старт EC, в резервном регистре нет x.
// Ожидается: выбран резистор 1,5 кОм, начата калибровка; заряд включается только после первого
// исправного замера питания МК, делитель включён, резервный регистр очищен.
static void test_init_without_saved_x_starts_calibration(void)
{
    mcu_vbat_init();

    TEST_ASSERT_TRUE_MESSAGE(PWR->CR4 & PWR_CR4_VBRS, "1.5 kOhm charge resistor must be selected on init");
    TEST_ASSERT_FALSE_MESSAGE(is_charge_on(), "Charging must wait for the first supply measurement");

    step();

    struct REGMAP_VBAT_STATUS s = get_status();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_CAL, s.state, "Calibration must start without saved x");
    TEST_ASSERT_TRUE_MESSAGE(is_charge_on(), "Calibration must charge when supply is OK");
    TEST_ASSERT_TRUE_MESSAGE(is_divider_on(), "Calibration must measure the node through the divider");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, s.x_mv, "x must be unknown during calibration");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FLAG_CHARGING, s.flags, "Only the charging bit is expected");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, rtc_get_tamper_reg(BKP_INDEX), "Backup register must be cleared");
}

// Сценарий: старт EC, в резервном регистре метка и x = 3273 мВ.
// Ожидается: x взят из регистра, y = x * 3000 / 3300, сразу суточная проверка с паузы:
// заряд и делитель выключены, регистр не тронут.
static void test_init_with_saved_x_starts_daily_check(void)
{
    start_with_saved_x(X_MV);

    struct REGMAP_VBAT_STATUS s = get_status();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_RELAX, s.state, "Daily check must start with the rest pause");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(X_MV, s.x_mv, "x must be restored from the backup register");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(Y_MV, s.y_mv, "y must be x * 3000 / 3300");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FLAG_CALIBRATED, s.flags, "Only the calibrated flag is expected");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, s.node_mv, "Node must read 0 while the divider is off");
    TEST_ASSERT_FALSE_MESSAGE(is_charge_on(), "Charging must be off in the rest pause");
    TEST_ASSERT_FALSE_MESSAGE(is_divider_on(), "Divider must be off in the rest pause");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        BKP_MAGIC | X_MV, rtc_get_tamper_reg(BKP_INDEX), "Backup register must keep the saved x"
    );
}

// Сценарий: в резервном регистре пусто, x вне 3000-3500 мВ, метка rc1 (0xCA1B) или чужие данные.
// Ожидается: регистр считается испорченным - калибровка, регистр очищен.
static void test_init_rejects_invalid_backup_register(void)
{
    const uint32_t invalid[] = {
        0,
        BKP_MAGIC | 2999,
        BKP_MAGIC | 3501,
        0xCA1B0000UL | X_MV,
        0xFFFF0000UL | X_MV,
    };

    for (unsigned i = 0; i < ARRAY_SIZE(invalid); i++) {
        char msg[80];
        reset_all();
        rtc_save_to_tamper_reg(BKP_INDEX, invalid[i]);

        start_calibration();

        snprintf(msg, sizeof(msg), "Backup 0x%08X must start calibration", (unsigned)invalid[i]);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_CAL, get_state(), msg);
        snprintf(msg, sizeof(msg), "Backup 0x%08X must be cleared", (unsigned)invalid[i]);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, rtc_get_tamper_reg(BKP_INDEX), msg);
    }
}

// Сценарий: в резервном регистре x на границах допустимого диапазона: 3000 и 3500 мВ.
// Ожидается: x принят, y пересчитан.
static void test_init_accepts_x_range_boundaries(void)
{
    start_with_saved_x(3000);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3000, get_status().x_mv, "x = 3000 mV must be accepted");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(2727, get_status().y_mv, "y must be 3000 * 3000 / 3300");

    reset_all();
    start_with_saved_x(3500);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3500, get_status().x_mv, "x = 3500 mV must be accepted");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3181, get_status().y_mv, "y must be 3500 * 3000 / 3300");
}

// Сценарий: АЦП ещё не готов (первый проход DMA после переинициализации на 64 МГц).
// Ожидается: модуль ничего не делает и не публикует состояние; как только АЦП готов - калибровка с зарядом.
static void test_periodic_work_waits_for_adc_ready(void)
{
    utest_adc_set_ready(false);
    mcu_vbat_init();

    run_ms(10 * MIN_MS);

    struct REGMAP_VBAT_STATUS s;
    TEST_ASSERT_FALSE_MESSAGE(is_charge_on(), "Charging must not start before ADC is ready");
    TEST_ASSERT_FALSE_MESSAGE(
        utest_regmap_get_region_data(REGMAP_REGION_VBAT_STATUS, &s, sizeof(s)),
        "Status must not be published before ADC is ready"
    );

    utest_adc_set_ready(true);
    step();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_CAL, get_state(), "Calibration must run once ADC is ready");
    TEST_ASSERT_TRUE_MESSAGE(is_charge_on(), "Calibration must charge once ADC is ready");
}

// ==================== Калибровка ====================

// Сценарий: узел с начала калибровки стоит на 3273 мВ, VDDA 3310 мВ (зазор 37 мВ).
// Ожидается: рост за 12 ч нулевой уже с 13-го часа, но раньше 24 ч плато не принимается;
// на 24-м часу x = 3273 сохранён в резервный регистр, заряд выключен, началась суточная проверка.
static void test_calibration_flat_node_near_vdda_finishes_after_24_hours(void)
{
    start_calibration();

    run_cal_hours(X_MV, 23);

    struct REGMAP_VBAT_STATUS s = get_status();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_CAL, s.state, "Plateau must not be accepted before 24 hours");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(23, s.cal_hours, "Calibration hours must be counted");
    TEST_ASSERT_TRUE_MESSAGE(is_charge_on(), "Calibration must keep charging");

    run_cal_hour(X_MV);

    s = get_status();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_RELAX, s.state, "Daily check must start after calibration");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(24, s.cal_hours, "Plateau must be found on hour 24");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(X_MV, s.x_mv, "x must be the plateau voltage");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(Y_MV, s.y_mv, "y must be x * 3000 / 3300");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FLAG_CALIBRATED, get_stored_flags(), "Only the calibrated flag is expected");
    TEST_ASSERT_FALSE_MESSAGE(is_charge_on(), "Charging must stop after calibration");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        BKP_MAGIC | X_MV, rtc_get_tamper_reg(BKP_INDEX), "x must be saved to the backup register"
    );
}

// Сценарий: узел растёт на 5 мВ в час 30 ч, дальше стоит на 3250 мВ; VDDA 3290 мВ.
// Ожидается: плато - когда узел не вырос за 12 ч: на 41-м часу рост ещё 5 мВ, на 42-м - 0, x = 3250.
static void test_calibration_waits_until_node_stops_growing_for_12_hours(void)
{
    utest_adc_set_vdda_mv(3290);
    start_calibration();

    for (uint16_t h = 1; h <= 41; h++) {
        uint16_t node = (h <= 30) ? (3100 + 5 * h) : 3250;
        run_cal_hour(node);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_CAL, get_state(), "Calibration must go on while the node grows");
    }
    TEST_ASSERT_EQUAL_INT16_MESSAGE(5, get_cal().growth_mv, "Growth for 12 hours must be reported");

    run_cal_hour(3250);

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_RELAX, get_state(), "Plateau must be found when growth is 0");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3250, get_status().x_mv, "x must be the plateau voltage");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(42, get_status().cal_hours, "Plateau must be found on hour 42");
}

// Сценарий: во втором часе калибровки узел полчаса 3270 мВ и полчаса 3276 мВ.
// Ожидается: точка часа - среднее всех отсчётов, 3273 мВ.
static void test_calibration_hour_point_is_average_of_samples(void)
{
    start_calibration();
    run_cal_hour(X_MV);

    set_node_mv(3270);
    run_ms(30 * MIN_MS);
    set_node_mv(3276);
    run_ms(30 * MIN_MS);

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3273, get_cal().u_mv, "Hour point must be the average of the hour");
}

// Сценарий: основной цикл крутится чаще раза в секунду; между ежесекундными отсчётами узел
// на лишних проходах цикла показывает 4000 мВ.
// Ожидается: в среднее часа идёт один отсчёт в секунду - точка часа 3273 мВ, лишние проходы не учтены.
static void test_calibration_takes_one_sample_per_second(void)
{
    start_calibration();
    run_cal_hour(X_MV);

    for (uint32_t t = 0; t < HOUR_MS; t += SEC_MS) {
        set_node_mv(X_MV);
        step();
        set_node_mv(4000);
        mcu_vbat_check_do_periodic_work();
    }

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(X_MV, get_cal().u_mv, "Only one sample per second must be averaged");
}

// Сценарий: калибровка, узел растёт на 1 мВ в час от 3201 мВ, VDDA 3310 мВ.
// Ожидается: регистры 0xE9-0xEC: до первого часа пусто; U и VDDA - последний час; рост за 12 ч
// не посчитан (INT16_MIN), пока нет 13 точек; зазор = VDDA - U.
static void test_calibration_reports_progress_in_cal_registers(void)
{
    start_calibration();

    struct REGMAP_VBAT_CAL c = get_cal();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, c.u_mv, "No hour point before the first hour");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(0, c.gap_mv, "No gap before the first hour");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(INT16_MIN, c.growth_mv, "Growth must be marked as not computed");

    for (uint16_t h = 1; h <= 12; h++) {
        run_cal_hour(3200 + h);
    }

    c = get_cal();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3212, c.u_mv, "U must be the last hour point");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(VDDA_OK_MV, c.vdda_mv, "VDDA must be the last hour average");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(INT16_MIN, c.growth_mv, "Growth needs 13 hour points");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(VDDA_OK_MV - 3212, c.gap_mv, "Gap must be VDDA - U");

    run_cal_hour(3213);

    c = get_cal();
    TEST_ASSERT_EQUAL_INT16_MESSAGE(12, c.growth_mv, "Growth must be U now minus U 12 hours ago");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(13, get_status().cal_hours, "Calibration hours must be counted");
}

// Сценарий: узел перестал расти на 3200 мВ, а VDDA 3310 мВ (зазор 110 > 60 мВ).
// Ожидается: ошибка калибровки и "ячейка не заряжается", заряд выкл., x не сохранён;
// через 24 ч калибровка заново, ошибки видны, пока она не найдёт плато.
static void test_calibration_flat_node_far_from_vdda_fails_and_retries_after_24_hours(void)
{
    start_calibration();

    run_cal_hours(3200, 24);

    struct REGMAP_VBAT_STATUS s = get_status();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_IDLE, s.state, "Calibration must stop on error");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FLAG_CAL_FAIL | FLAG_CELL_FAIL, get_stored_flags(), "Cell failure expected");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, s.x_mv, "x must not be taken on error");
    TEST_ASSERT_FALSE_MESSAGE(is_charge_on(), "Charging must stop on calibration error");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, rtc_get_tamper_reg(BKP_INDEX), "x must not be saved on error");

    run_ms(24 * HOUR_MS - SEC_MS);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_IDLE, get_state(), "Retry must wait 24 hours");
    TEST_ASSERT_FALSE_MESSAGE(is_charge_on(), "Charging must stay off while waiting");

    step();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_CAL, get_state(), "Calibration must restart after 24 hours");
    TEST_ASSERT_TRUE_MESSAGE(is_charge_on(), "Restarted calibration must charge");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        FLAG_CAL_FAIL | FLAG_CELL_FAIL, get_stored_flags(), "Previous error must stay visible"
    );

    run_cal_hours(X_MV, 24);

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_RELAX, get_state(), "Retry must find the plateau");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FLAG_CALIBRATED, get_stored_flags(), "Errors must clear on success");
}

// Сценарий: плато, зазор VDDA - U ровно 60 мВ, затем 61 мВ.
// Ожидается: 60 мВ - узел у питания, x принят; 61 мВ - ошибка "ячейка не заряжается".
static void test_calibration_gap_threshold_boundary(void)
{
    utest_adc_set_vdda_mv(X_MV + 60);
    start_calibration();
    run_cal_hours(X_MV, 24);

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_RELAX, get_state(), "Gap of 60 mV must be accepted");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(X_MV, get_status().x_mv, "x must be taken with gap of 60 mV");

    reset_all();
    utest_adc_set_vdda_mv(X_MV + 61);
    start_calibration();
    run_cal_hours(X_MV, 24);

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_IDLE, get_state(), "Gap of 61 mV must fail calibration");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FLAG_CAL_FAIL | FLAG_CELL_FAIL, get_stored_flags(), "Cell failure expected");
}

// Сценарий: узел растёт на 1 мВ в час (рост за 12 ч 12 мВ - плато нет), к 96-му часу 3296 мВ, VDDA 3310.
// Ожидается: на 96-м часу зазор 14 мВ <= 60: x = U, флаг "x по зазору", суточная проверка.
static void test_calibration_without_plateau_takes_x_by_gap_after_96_hours(void)
{
    start_calibration();

    for (uint16_t h = 1; h <= 95; h++) {
        run_cal_hour(3200 + h);
    }
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_CAL, get_state(), "Calibration must last up to 96 hours");

    run_cal_hour(3296);

    struct REGMAP_VBAT_STATUS s = get_status();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_RELAX, s.state, "Daily check must start after 96 hours");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3296, s.x_mv, "x must be the last hour point");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(2996, s.y_mv, "y must be x * 3000 / 3300");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        FLAG_CALIBRATED | FLAG_CAL_BY_GAP, get_stored_flags(), "x by gap must be flagged"
    );
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        BKP_MAGIC | 3296, rtc_get_tamper_reg(BKP_INDEX), "x by gap must be saved"
    );
}

// Сценарий: узел растёт на 1 мВ в час и к 96-му часу только 3096 мВ, VDDA 3310 (зазор 214 мВ).
// Ожидается: ошибка калибровки без "ячейка не заряжается", заряд выкл., x не сохранён.
static void test_calibration_without_plateau_far_from_vdda_fails_after_96_hours(void)
{
    start_calibration();

    for (uint16_t h = 1; h <= 96; h++) {
        run_cal_hour(3000 + h);
    }

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_IDLE, get_state(), "Calibration must stop after 96 hours");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FLAG_CAL_FAIL, get_stored_flags(), "Only calibration error expected");
    TEST_ASSERT_FALSE_MESSAGE(is_charge_on(), "Charging must stop on calibration error");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, rtc_get_tamper_reg(BKP_INDEX), "x must not be saved on error");
}

// Сценарий: после 10 ч калибровки питание МК 5 ч ниже 3200 мВ (EC от WBMZ), потом вернулось.
// Ожидается: на паузе заряд выкл., флаг LOW_SUPPLY, часы не идут, ошибки нет; после возврата
// заряд включается и час начинается заново: плато через 14 полных часов.
static void test_calibration_pauses_on_low_supply_and_restarts_hour(void)
{
    start_calibration();
    run_cal_hours(X_MV, 10);

    utest_adc_set_vdda_mv(VDDA_LOW_MV);
    run_ms(5 * HOUR_MS);

    struct REGMAP_VBAT_STATUS s = get_status();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_CAL, s.state, "Low supply must pause calibration, not stop it");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(10, s.cal_hours, "Hours must not be counted on low supply");
    TEST_ASSERT_FALSE_MESSAGE(is_charge_on(), "Charging must be off on low supply");
    TEST_ASSERT_TRUE_MESSAGE(s.flags & FLAG_LOW_SUPPLY, "Low supply must be shown");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0, get_stored_flags(), "Low supply is not an error");

    utest_adc_set_vdda_mv(VDDA_OK_MV);
    step();
    TEST_ASSERT_TRUE_MESSAGE(is_charge_on(), "Charging must resume with supply");
    TEST_ASSERT_FALSE_MESSAGE(get_status().flags & FLAG_LOW_SUPPLY, "Low supply flag must clear");

    run_cal_hours(X_MV, 13);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_CAL, get_state(), "Plateau needs 24 counted hours");

    run_cal_hour(X_MV);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_RELAX, get_state(), "Plateau must be found on hour 24");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(24, get_status().cal_hours, "Only full hours must be counted");
}

// Сценарий: старт калибровки при VDDA ровно 3200 мВ, затем 3199 мВ.
// Ожидается: 3200 - питание в норме, заряд включён; 3199 - низкое питание: заряда нет, флаг LOW_SUPPLY.
static void test_low_supply_threshold_boundary(void)
{
    utest_adc_set_vdda_mv(3200);
    start_calibration();

    TEST_ASSERT_TRUE_MESSAGE(is_charge_on(), "VDDA of 3200 mV must allow charging");
    TEST_ASSERT_FALSE_MESSAGE(get_status().flags & FLAG_LOW_SUPPLY, "VDDA of 3200 mV is not low supply");

    reset_all();
    utest_adc_set_vdda_mv(3199);
    start_calibration();

    TEST_ASSERT_FALSE_MESSAGE(is_charge_on(), "VDDA of 3199 mV must not allow charging");
    TEST_ASSERT_TRUE_MESSAGE(get_status().flags & FLAG_LOW_SUPPLY, "VDDA of 3199 mV is low supply");
}

// Сценарий: час калибровки при VDDA на границах исправного VREFINT (2500, 3600 мВ) и сразу за ними.
// Ожидается: 2500 и 3600 мВ - отсчёты исправны, ошибки АЦП нет; 2499 и 3601 мВ - через час ошибка АЦП.
static void test_adc_fault_range_boundaries(void)
{
    const struct {
        int32_t vdda_mv;
        bool fault;
    } cases[] = {
        {2499, true},
        {2500, false},
        {3600, false},
        {3601, true},
    };

    for (unsigned i = 0; i < ARRAY_SIZE(cases); i++) {
        char msg[80];
        reset_all();
        utest_adc_set_vdda_mv(cases[i].vdda_mv);
        set_node_mv(X_MV);
        start_calibration();

        run_ms(HOUR_MS + MIN_MS);

        uint16_t expected = cases[i].fault ? (FLAG_CAL_FAIL | FLAG_ADC_FAIL) : 0;
        snprintf(msg, sizeof(msg), "VDDA %d mV: unexpected ADC error state", (int)cases[i].vdda_mv);
        TEST_ASSERT_EQUAL_HEX16_MESSAGE(expected, get_stored_flags(), msg);
    }
}

// Сценарий: на 3-м часу калибровки VDDA по VREFINT ушёл ниже 2500 мВ (сбой АЦП) и не вернулся.
// Ожидается: отсчёты не берутся, заряд выкл.; через час без исправного отсчёта - ошибка калибровки и АЦП.
static void test_calibration_adc_fault_for_hour_fails(void)
{
    start_calibration();
    run_cal_hours(X_MV, 2);

    utest_adc_set_vdda_mv(VDDA_FAULT_LOW_MV);
    run_ms(59 * MIN_MS);

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_CAL, get_state(), "ADC error must wait for an hour");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0, get_stored_flags(), "No error before an hour without samples");
    TEST_ASSERT_FALSE_MESSAGE(is_charge_on(), "No charging without a valid supply sample");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(2, get_status().cal_hours, "Hours must not be counted without samples");

    run_ms(2 * MIN_MS);

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_IDLE, get_state(), "Calibration must stop on ADC error");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FLAG_CAL_FAIL | FLAG_ADC_FAIL, get_stored_flags(), "ADC error expected");
    TEST_ASSERT_FALSE_MESSAGE(is_charge_on(), "Charging must stay off on ADC error");
}

// ==================== Суточная проверка ====================

// Сценарий: x есть, ячейка 3000 мВ (выше y = 2975).
// Ожидается: пауза 10 мин без делителя, замер 2 мин (первые 5 с делитель устанавливается: узел 0,
// фильтр АЦП перезапускается), ячейка заряжена - заряд не нужен, делитель выкл.; следующая проверка
// через 24 ч от начала этой.
static void test_check_charged_cell_measures_and_waits_next_day(void)
{
    set_node_mv(3000);
    start_with_saved_x(X_MV);

    run_ms(10 * MIN_MS + 2 * SEC_MS);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_MEASURE, get_state(), "Measurement must follow a 10 min pause");
    TEST_ASSERT_TRUE_MESSAGE(is_divider_on(), "Divider must be on for the measurement");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, get_status().node_mv, "Node must read 0 while the divider settles");
    TEST_ASSERT_TRUE_MESSAGE(
        utest_adc_get_reset_lowpass_count(ADC_CHANNEL_ADC_INT_VBAT) > 0,
        "ADC filter must be restarted while the divider settles"
    );

    run_ms(3 * SEC_MS);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3000, get_status().node_mv, "Node must be shown after settling");

    run_until_state(STATE_BIT(ST_IDLE) | STATE_BIT(ST_CHARGE), CHECK_DECISION_TIMEOUT_MS);

    struct REGMAP_VBAT_STATUS s = get_status();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_IDLE, s.state, "Charged cell needs no charge");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(12 * MIN_MS, systick_get_system_time_ms(), "Check must take 12 min");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3000, s.cell_mv, "Cell voltage must be the measured average");
    TEST_ASSERT_FALSE_MESSAGE(is_divider_on(), "Divider must be off after the check");
    TEST_ASSERT_FALSE_MESSAGE(is_charge_on(), "Charging must stay off");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FLAG_CALIBRATED, get_stored_flags(), "No errors expected");

    run_ms(5 * MIN_MS);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(5, get_status().state_min, "Minutes in state must be shown");

    run_ms(24 * HOUR_MS - 17 * MIN_MS - SEC_MS);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_IDLE, get_state(), "Next check must wait 24 hours from this one");

    step();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_RELAX, get_state(), "Next check must start 24 hours after this one");
}

// Сценарий: ячейка 2950 мВ (ниже y), после порции заряда - 2990 мВ.
// Ожидается: порция 15 мин с включённым зарядом, пауза, замер; ячейка дошла до y - проверка закончена.
static void test_check_low_cell_charges_portion_until_threshold(void)
{
    set_node_mv(2950);
    start_with_saved_x(X_MV);

    uint16_t state = run_until_state(STATE_BIT(ST_CHARGE) | STATE_BIT(ST_IDLE), CHECK_DECISION_TIMEOUT_MS);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_CHARGE, state, "Cell below y must be charged");
    TEST_ASSERT_TRUE_MESSAGE(is_charge_on(), "Charging must be on during the portion");
    TEST_ASSERT_TRUE_MESSAGE(get_status().flags & FLAG_CHARGING, "Charging must be shown in the status");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(2950, get_status().cell_mv, "Measured cell voltage must be shown");
    systime_t portion_start = systick_get_system_time_ms();

    set_node_mv(2990);
    run_until_state(STATE_BIT(ST_RELAX), PORTION_TIMEOUT_MS);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        15 * MIN_MS, systick_get_system_time_ms() - portion_start, "Charge portion must last 15 min"
    );
    TEST_ASSERT_FALSE_MESSAGE(is_charge_on(), "Charging must stop after the portion");

    state = run_until_state(STATE_BIT(ST_CHARGE) | STATE_BIT(ST_IDLE), CHECK_DECISION_TIMEOUT_MS);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_IDLE, state, "Check must end when the cell reaches y");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(2990, get_status().cell_mv, "Cell after the portion must be shown");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FLAG_CALIBRATED, get_stored_flags(), "No errors expected");
}

// Сценарий: при проверке ячейка ровно на пороге y, затем на 1 мВ ниже.
// Ожидается: ровно y - заряжена, проверка закончена; y - 1 - порция заряда.
static void test_check_threshold_boundary(void)
{
    uint32_t decision_mask = STATE_BIT(ST_CHARGE) | STATE_BIT(ST_IDLE);

    set_node_mv(Y_MV);
    start_with_saved_x(X_MV);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        ST_IDLE, run_until_state(decision_mask, CHECK_DECISION_TIMEOUT_MS), "Cell at y must not be charged"
    );

    reset_all();
    set_node_mv(Y_MV - 1);
    start_with_saved_x(X_MV);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        ST_CHARGE, run_until_state(decision_mask, CHECK_DECISION_TIMEOUT_MS), "Cell below y must be charged"
    );
}

// Сценарий: во время замера ячейки VREFINT сбоит 3 мин - исправных отсчётов нет.
// Ожидается: без отсчётов замер не заканчивается; решение - на первом исправном отсчёте.
static void test_check_measurement_waits_for_valid_sample(void)
{
    set_node_mv(3000);
    start_with_saved_x(X_MV);
    run_until_state(STATE_BIT(ST_MEASURE), CHECK_DECISION_TIMEOUT_MS);

    utest_adc_set_vdda_mv(VDDA_FAULT_LOW_MV);
    run_ms(3 * MIN_MS);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_MEASURE, get_state(), "Measurement must wait for a valid sample");

    utest_adc_set_vdda_mv(VDDA_OK_MV);
    step();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_IDLE, get_state(), "Decision must follow the first valid sample");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3000, get_status().cell_mv, "Cell must be the valid sample");
}

static uint16_t cell_after_48_portions_mv;

static void cell_jumps_after_48_portions(uint32_t portion)
{
    if (portion == 48) {
        set_node_mv(cell_after_48_portions_mv);
    }
}

// Сценарий: ячейка стоит на 2900 мВ и не растёт от заряда.
// Ожидается: после 12 ч реального заряда (48 порций по 15 мин) - ошибка заряда и "ячейка не
// заряжается", заряд выкл.; следующая проверка через 24 ч от ошибки.
static void test_check_cell_without_growth_for_12_hours_of_charge_fails(void)
{
    set_node_mv(2900);
    start_with_saved_x(X_MV);

    uint32_t portions = run_check_portions(NULL);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(48, portions, "Growth must be checked after 12 hours of charge");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_IDLE, get_state(), "Check must stop on charge error");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        FLAG_CALIBRATED | FLAG_CHARGE_FAIL | FLAG_CELL_FAIL, get_stored_flags(), "Cell failure expected"
    );
    TEST_ASSERT_FALSE_MESSAGE(is_charge_on(), "Charging must stop on charge error");

    run_ms(24 * HOUR_MS - SEC_MS);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_IDLE, get_state(), "Retry must wait 24 hours from the error");

    step();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_RELAX, get_state(), "Check must be retried after 24 hours");
}

// Сценарий: за 12 ч заряда ячейка выросла ровно на 20 мВ (2900 -> 2920).
// Ожидается: рост не больше 20 мВ - ошибка "ячейка не заряжается" после 48-й порции.
static void test_check_growth_of_20_mv_for_12_hours_is_no_growth(void)
{
    cell_after_48_portions_mv = 2920;
    set_node_mv(2900);
    start_with_saved_x(X_MV);

    uint32_t portions = run_check_portions(cell_jumps_after_48_portions);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(48, portions, "20 mV growth must count as no growth");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        FLAG_CALIBRATED | FLAG_CHARGE_FAIL | FLAG_CELL_FAIL, get_stored_flags(), "Cell failure expected"
    );
}

// Сценарий: за первые 12 ч заряда ячейка выросла на 21 мВ (2900 -> 2921), дальше не растёт.
// Ожидается: первая проверка роста пройдена, точка отсчёта сдвинута; ошибка - после вторых 12 ч (96 порций).
static void test_check_growth_of_21_mv_moves_reference_point(void)
{
    cell_after_48_portions_mv = 2921;
    set_node_mv(2900);
    start_with_saved_x(X_MV);

    uint32_t portions = run_check_portions(cell_jumps_after_48_portions);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(96, portions, "Growth must be compared with the point 12 hours before");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        FLAG_CALIBRATED | FLAG_CHARGE_FAIL | FLAG_CELL_FAIL, get_stored_flags(), "Cell failure expected"
    );
}

static void cell_grows_1_mv_per_portion(uint32_t portion)
{
    (void)portion;
    cell_mv++;
    set_node_mv(cell_mv);
}

// Сценарий: ячейка от 2500 мВ растёт на 1 мВ за порцию (48 мВ за 12 ч - заряжается), но до y далеко.
// Ожидается: после 96 ч реального заряда (384 порции) - ошибка заряда без "ячейка не заряжается".
static void test_check_charge_limit_96_hours_fails(void)
{
    cell_mv = 2500;
    set_node_mv(cell_mv);
    start_with_saved_x(X_MV);

    uint32_t portions = run_check_portions(cell_grows_1_mv_per_portion);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(384, portions, "Charge must be limited to 96 hours");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(2884, get_status().cell_mv, "Last measured cell voltage must be shown");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        FLAG_CALIBRATED | FLAG_CHARGE_FAIL, get_stored_flags(), "Charge error without cell failure expected"
    );
    TEST_ASSERT_FALSE_MESSAGE(is_charge_on(), "Charging must stop on charge error");
}

// Сценарий: ячейка ниже y, а питание МК 3100 мВ (EC от WBMZ); потом питание вернулось.
// Ожидается: замер идёт, но заряда нет - снова пауза и замер, флаг LOW_SUPPLY, ошибки нет;
// с питанием - порция заряда.
static void test_check_low_supply_postpones_charge(void)
{
    set_node_mv(2950);
    utest_adc_set_vdda_mv(VDDA_LOW_MV);
    start_with_saved_x(X_MV);

    run_until_state(STATE_BIT(ST_MEASURE), CHECK_DECISION_TIMEOUT_MS);
    uint16_t state = run_until_state(~STATE_BIT(ST_MEASURE), CHECK_DECISION_TIMEOUT_MS);

    struct REGMAP_VBAT_STATUS s = get_status();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_RELAX, state, "Low supply must postpone charging to the next measurement");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(2950, s.cell_mv, "Cell must be measured on low supply");
    TEST_ASSERT_TRUE_MESSAGE(s.flags & FLAG_LOW_SUPPLY, "Low supply must be shown");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FLAG_CALIBRATED, get_stored_flags(), "Low supply is not an error");
    TEST_ASSERT_FALSE_MESSAGE(is_charge_on(), "No charging on low supply");

    utest_adc_set_vdda_mv(VDDA_OK_MV);
    state = run_until_state(STATE_BIT(ST_CHARGE) | STATE_BIT(ST_IDLE), CHECK_DECISION_TIMEOUT_MS);

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_CHARGE, state, "Charging must start when supply is back");
    TEST_ASSERT_TRUE_MESSAGE(is_charge_on(), "Charging must be on");
}

// Сценарий: ячейка не растёт, и в каждой порции через 5 мин питание МК падает ниже 3200 мВ.
// Ожидается: порция обрывается, в счёт заряда идёт реальное время, а не число порций: проверка
// роста - после 12 ч реального заряда, т.е. после 144 коротких порций, а не после 48.
static void test_check_charge_portion_cut_by_low_supply_counts_real_time(void)
{
    // Низкое питание модуль замечает на следующем ежесекундном отсчёте
    const uint32_t portion_ms = 5 * MIN_MS + SEC_MS;
    const uint32_t expected_portions = (12 * HOUR_MS + portion_ms - 1) / portion_ms;
    uint32_t decision_mask = STATE_BIT(ST_CHARGE) | STATE_BIT(ST_IDLE);
    uint32_t portions = 0;

    set_node_mv(2900);
    start_with_saved_x(X_MV);

    uint16_t state = run_until_state(decision_mask, CHECK_DECISION_TIMEOUT_MS);
    while (state == ST_CHARGE) {
        portions++;
        systime_t portion_start = systick_get_system_time_ms();
        run_ms(5 * MIN_MS);
        utest_adc_set_vdda_mv(VDDA_LOW_MV);
        run_until_state(STATE_BIT(ST_RELAX), MIN_MS);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(
            portion_ms, systick_get_system_time_ms() - portion_start, "Portion must stop on low supply"
        );
        TEST_ASSERT_FALSE_MESSAGE(is_charge_on(), "Charging must stop on low supply");
        utest_adc_set_vdda_mv(VDDA_OK_MV);
        state = run_until_state(decision_mask, CHECK_DECISION_TIMEOUT_MS);
    }

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected_portions, portions, "Real charge time must be counted");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        FLAG_CALIBRATED | FLAG_CHARGE_FAIL | FLAG_CELL_FAIL, get_stored_flags(), "Cell failure expected"
    );
}

// Сценарий: проверка закончилась, после неё VDDA по VREFINT ушёл выше 3600 мВ (сбой АЦП) на час.
// Ожидается: ошибка заряда и АЦП, следующая проверка через 24 ч от ошибки; когда АЦП исправен
// и ячейка заряжена, проверка снимает ошибки.
static void test_check_adc_fault_for_hour_sets_charge_fail_and_retries_next_day(void)
{
    set_node_mv(3000);
    start_with_saved_x(X_MV);
    run_until_state(STATE_BIT(ST_IDLE), CHECK_DECISION_TIMEOUT_MS);

    utest_adc_set_vdda_mv(VDDA_FAULT_HIGH_MV);
    run_ms(HOUR_MS - SEC_MS);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FLAG_CALIBRATED, get_stored_flags(), "ADC error must wait for an hour");

    step();
    systime_t error_time = systick_get_system_time_ms();
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        FLAG_CALIBRATED | FLAG_CHARGE_FAIL | FLAG_ADC_FAIL, get_stored_flags(), "ADC error expected"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_IDLE, get_state(), "Check must wait after ADC error");

    utest_adc_set_vdda_mv(VDDA_OK_MV);
    run_ms(24 * HOUR_MS - SEC_MS);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_IDLE, get_state(), "Retry must wait 24 hours from the error");

    step();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        24 * HOUR_MS, systick_get_system_time_ms() - error_time, "Retry must start 24 hours after the error"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_RELAX, get_state(), "Check must be retried after 24 hours");

    run_until_state(STATE_BIT(ST_IDLE), CHECK_DECISION_TIMEOUT_MS);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FLAG_CALIBRATED, get_stored_flags(), "Successful check must clear errors");
}

// ==================== Команды 0xE8 ====================

// Сценарий: x есть, проверка закончена; команда START_CAL.
// Ожидается: x и y стёрты, резервный регистр очищен, калибровка с зарядом; регистр команды обнулён.
static void test_cmd_start_cal_restarts_calibration(void)
{
    set_node_mv(3000);
    start_with_saved_x(X_MV);
    run_until_state(STATE_BIT(ST_IDLE), CHECK_DECISION_TIMEOUT_MS);

    send_cmd(CMD_START_CAL);
    step();

    struct REGMAP_VBAT_STATUS s = get_status();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_CAL, s.state, "START_CAL must start calibration");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, s.x_mv, "START_CAL must forget x");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, s.y_mv, "START_CAL must forget y");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0, get_stored_flags(), "Calibrated flag must be cleared");
    TEST_ASSERT_TRUE_MESSAGE(is_charge_on(), "Calibration must charge");
    TEST_ASSERT_TRUE_MESSAGE(is_divider_on(), "Calibration must measure the node");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, rtc_get_tamper_reg(BKP_INDEX), "Backup register must be cleared");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, get_cmd(), "Command register must be cleared");
}

// Сценарий: x есть, ждём суточной проверки; команда CHECK_NOW.
// Ожидается: проверка начинается сразу.
static void test_cmd_check_now_starts_check(void)
{
    set_node_mv(3000);
    start_with_saved_x(X_MV);
    run_until_state(STATE_BIT(ST_IDLE), CHECK_DECISION_TIMEOUT_MS);
    run_ms(HOUR_MS);

    send_cmd(CMD_CHECK_NOW);
    step();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_RELAX, get_state(), "CHECK_NOW must start the check");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, get_cmd(), "Command register must be cleared");
}

// Сценарий: идёт калибровка, x нет; команда CHECK_NOW.
// Ожидается: команда игнорируется - без x проверять не с чем, калибровка продолжается.
static void test_cmd_check_now_ignored_without_x(void)
{
    start_calibration();

    send_cmd(CMD_CHECK_NOW);
    step();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_CAL, get_state(), "CHECK_NOW must be ignored without x");
    TEST_ASSERT_TRUE_MESSAGE(is_charge_on(), "Calibration must go on");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, get_cmd(), "Command register must be cleared");
}

// Сценарий: x есть, висят ошибки заряда и АЦП; команда FORGET_CAL.
// Ожидается: x и все флаги стёрты, калибровка с нуля.
static void test_cmd_forget_cal_clears_x_and_errors(void)
{
    set_node_mv(3000);
    start_with_saved_x(X_MV);
    run_until_state(STATE_BIT(ST_IDLE), CHECK_DECISION_TIMEOUT_MS);
    utest_adc_set_vdda_mv(VDDA_FAULT_LOW_MV);
    run_ms(HOUR_MS + MIN_MS);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        FLAG_CALIBRATED | FLAG_CHARGE_FAIL | FLAG_ADC_FAIL, get_stored_flags(), "Errors must be set before command"
    );
    utest_adc_set_vdda_mv(VDDA_OK_MV);

    send_cmd(CMD_FORGET_CAL);
    step();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_CAL, get_state(), "FORGET_CAL must start calibration");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0, get_stored_flags(), "FORGET_CAL must clear all flags");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, get_status().x_mv, "FORGET_CAL must forget x");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, rtc_get_tamper_reg(BKP_INDEX), "Backup register must be cleared");
    TEST_ASSERT_TRUE_MESSAGE(is_charge_on(), "Calibration must charge");
}

// Сценарий: в регистр команды записано неизвестное значение.
// Ожидается: состояние не меняется, регистр команды обнулён.
static void test_cmd_unknown_is_ignored(void)
{
    set_node_mv(3000);
    start_with_saved_x(X_MV);
    run_until_state(STATE_BIT(ST_IDLE), CHECK_DECISION_TIMEOUT_MS);

    send_cmd(0x55);
    step();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ST_IDLE, get_state(), "Unknown command must be ignored");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(FLAG_CALIBRATED, get_stored_flags(), "Flags must not change");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, get_cmd(), "Command register must be cleared");
}

// ==================== Публикация в regmap ====================

// Сценарий: на границе часа калибровки regmap занят SPI-транзакцией.
// Ожидается: новые данные не теряются - публикуются на первом проходе после освобождения,
// хотя в этом проходе в них ничего не изменилось.
static void test_status_published_after_regmap_busy(void)
{
    start_calibration();
    set_node_mv(X_MV);
    run_ms(HOUR_MS - SEC_MS);

    utest_regmap_set_busy(true);
    step();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, get_status().cal_hours, "Busy regmap must not be written");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, get_cal().u_mv, "Busy regmap must not be written");

    utest_regmap_set_busy(false);
    step();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, get_status().cal_hours, "Status must be published after busy regmap");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(X_MV, get_cal().u_mv, "Calibration data must be published after busy regmap");
}

// ==================== Резервный регистр и standby ====================

// Сценарий: x есть, а резервный регистр стёрли при работающем EC (reset_rtc, tamper).
// Ожидается: на следующем проходе цикла x записан обратно.
static void test_backup_register_restored_after_erase(void)
{
    start_with_saved_x(X_MV);

    rtc_save_to_tamper_reg(BKP_INDEX, 0);
    step();

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        BKP_MAGIC | X_MV, rtc_get_tamper_reg(BKP_INDEX), "x must be written back to the backup register"
    );
}

// Сценарий: идёт калибровка (заряд и делитель включены), EC уходит в standby.
// Ожидается: заряд и делитель выключены - в standby заряд никто не контролирует.
static void test_prepare_standby_turns_off_charge_and_divider(void)
{
    start_calibration();
    TEST_ASSERT_TRUE_MESSAGE(is_charge_on(), "Calibration must charge before standby");
    TEST_ASSERT_TRUE_MESSAGE(is_divider_on(), "Divider must be on before standby");

    mcu_vbat_prepare_standby();

    TEST_ASSERT_FALSE_MESSAGE(is_charge_on(), "Charging must be off in standby");
    TEST_ASSERT_FALSE_MESSAGE(is_divider_on(), "Divider must be off in standby");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_init_without_saved_x_starts_calibration);
    RUN_TEST(test_init_with_saved_x_starts_daily_check);
    RUN_TEST(test_init_rejects_invalid_backup_register);
    RUN_TEST(test_init_accepts_x_range_boundaries);
    RUN_TEST(test_periodic_work_waits_for_adc_ready);

    RUN_TEST(test_calibration_flat_node_near_vdda_finishes_after_24_hours);
    RUN_TEST(test_calibration_waits_until_node_stops_growing_for_12_hours);
    RUN_TEST(test_calibration_hour_point_is_average_of_samples);
    RUN_TEST(test_calibration_takes_one_sample_per_second);
    RUN_TEST(test_calibration_reports_progress_in_cal_registers);
    RUN_TEST(test_calibration_flat_node_far_from_vdda_fails_and_retries_after_24_hours);
    RUN_TEST(test_calibration_gap_threshold_boundary);
    RUN_TEST(test_calibration_without_plateau_takes_x_by_gap_after_96_hours);
    RUN_TEST(test_calibration_without_plateau_far_from_vdda_fails_after_96_hours);
    RUN_TEST(test_calibration_pauses_on_low_supply_and_restarts_hour);
    RUN_TEST(test_low_supply_threshold_boundary);
    RUN_TEST(test_adc_fault_range_boundaries);
    RUN_TEST(test_calibration_adc_fault_for_hour_fails);

    RUN_TEST(test_check_charged_cell_measures_and_waits_next_day);
    RUN_TEST(test_check_low_cell_charges_portion_until_threshold);
    RUN_TEST(test_check_threshold_boundary);
    RUN_TEST(test_check_measurement_waits_for_valid_sample);
    RUN_TEST(test_check_cell_without_growth_for_12_hours_of_charge_fails);
    RUN_TEST(test_check_growth_of_20_mv_for_12_hours_is_no_growth);
    RUN_TEST(test_check_growth_of_21_mv_moves_reference_point);
    RUN_TEST(test_check_charge_limit_96_hours_fails);
    RUN_TEST(test_check_low_supply_postpones_charge);
    RUN_TEST(test_check_charge_portion_cut_by_low_supply_counts_real_time);
    RUN_TEST(test_check_adc_fault_for_hour_sets_charge_fail_and_retries_next_day);

    RUN_TEST(test_cmd_start_cal_restarts_calibration);
    RUN_TEST(test_cmd_check_now_starts_check);
    RUN_TEST(test_cmd_check_now_ignored_without_x);
    RUN_TEST(test_cmd_forget_cal_clears_x_and_errors);
    RUN_TEST(test_cmd_unknown_is_ignored);

    RUN_TEST(test_status_published_after_regmap_busy);

    RUN_TEST(test_backup_register_restored_after_erase);
    RUN_TEST(test_prepare_standby_turns_off_charge_and_divider);

    return UNITY_END();
}
