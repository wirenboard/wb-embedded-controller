#include "unity.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "wbec.h"
#include "wdt.h"
#include "linux-power-control.h"
#include "voltage-monitor.h"
#include "regmap-int.h"
#include "mcu-pwr.h"
#include "utest_gpio.h"
#include "utest_mcu_pwr.h"
#include "utest_pwrkey.h"
#include "utest_regmap.h"
#include "utest_rtc.h"
#include "utest_system_led.h"
#include "utest_systick.h"
#include "utest_voltage_monitor.h"
#include "utest_wbmz_common.h"
#include "utest_irq.h"

/**
 * Интеграционный тест: реальные wbec.c + linux-power-control.c + wdt.c
 * работают вместе, как в main loop прошивки.
 *
 * Поверх модулей построена модель платы:
 *  - линия +5В процессорного модуля управляется EC_GPIO_LINUX_POWER;
 *  - PMIC: 3.3В появляется через PMIC_START_MS после подачи 5В
 *    и пропадает через V33_DECAY_MS после снятия;
 *  - PMIC может "зависнуть" (авария): 3.3В пропадает и не возвращается,
 *    пока PMIC не перезапустят снятием 5В или удержанием PWRON;
 *  - линия PMIC RESET (PWROK): на WB85 (WBEC_HAS_WARM_RESET) удерживает
 *    в сбросе только SoC (PMIC игнорирует), на WB74 сбрасывает PMIC
 *    и 3.3В пропадает;
 *  - SoC "работает", когда есть 5В, 3.3В и линия сброса отпущена;
 *    может кормить watchdog через regmap, может "зависать".
 *
 * Главные проверяемые инварианты (свойства отсутствия клина):
 *  1. Линия PMIC RESET (PWROK) никогда не удерживается дольше штатной
 *     длительности (иначе SoC заклинен в сбросе навсегда - именно такой
 *     отказ наблюдался на стенде: 5В есть, SPL/U-Boot не стартует,
 *     watchdog больше не перезагружает плату).
 *  2. Если Linux не кормит watchdog и переход в standby не запрошен,
 *     состояние "SoC может стартовать" (5В + 3.3В + сброс отпущен)
 *     обязано наступать не реже, чем раз в WBEC_WATCHDOG_INITIAL_TIMEOUT_S
 *     с запасом. Т.е. таймаут watchdog всегда приводит к восстановлению.
 */

static const gpio_pin_t gpio_5v = { EC_GPIO_LINUX_POWER };
static const gpio_pin_t gpio_pwron = { EC_GPIO_LINUX_PMIC_PWRON };
static const gpio_pin_t gpio_nrst = { EC_GPIO_LINUX_PMIC_RESET_PWROK };

// Времена модели платы, мс
#define PMIC_START_MS               100     // 5В подано -> 3.3В появилось
#define V33_DECAY_MS                30      // 5В снято -> 3.3В пропало
#define PMIC_REVIVE_5V_OFF_MS       300     // столько без 5В перезапускает зависший PMIC
#define PMIC_REVIVE_PWRON_MS        100     // столько с PWRON перезапускает зависший PMIC
#define SOC_FEED_UPTIME_MS          3000    // аптайм SoC, на котором Linux кормит watchdog

// Допустимые пределы инвариантов
#if defined(WBEC_HAS_WARM_RESET)
    // Тёплый сброс: короткий импульс + запас
    #define NRST_ASSERTED_LIMIT_MS  (WBEC_WARM_RESET_PULSE_MS + 100)
#else
    // Исторический сброс PMIC: удержание до 2 с + запас
    #define NRST_ASSERTED_LIMIT_MS  (2000 + 100)
#endif
#define NOT_STARTABLE_LIMIT_MS      ((WBEC_WATCHDOG_INITIAL_TIMEOUT_S * 1000) + 15000)

struct sim {
    uint32_t now;                       // сим-время, мс

    // Модель PMIC
    bool v33;
    bool pmic_crashed;
    uint32_t v33_rise_cnt;
    uint32_t v33_fall_cnt;
    uint32_t revive_5v_off_cnt;
    uint32_t revive_pwron_cnt;

    // Модель SoC
    bool soc_feeds;                     // Linux жив и кормит watchdog
    uint32_t soc_hang_at_uptime_ms;     // аптайм, на котором Linux зависает (0 = не зависает)
    uint32_t soc_run_since;             // 0 = SoC в сбросе/без питания
    uint32_t soc_boot_count;            // сколько раз SoC начинал загрузку

    // Отложенные события
    uint32_t pmic_crash_at;             // авария PMIC в этот момент сим-времени (0 = нет)
    uint32_t powerctrl_at;              // запись в POWER_CTRL в этот момент (0 = нет)
    bool powerctrl_off;
    bool powerctrl_reboot;
    bool powerctrl_reset_pmic;

    // Наблюдения
    bool prev_p5v;
    bool prev_nrst;
    uint32_t rail_off_edges;            // 5В: переходы 1 -> 0
    uint32_t warm_pulses;               // завершённые импульсы на линии сброса при включённом 5В

    // Инварианты
    bool check_invariants;
    bool allow_standby;
    uint32_t nrst_asserted_ms;
    uint32_t max_nrst_asserted_ms;
    uint32_t not_startable_ms;
    uint32_t max_not_startable_ms;
    bool standby_requested;
};

static struct sim sim;

// Момент последнего входа wbec в состояние WORKING (перехват weak-функции)
static uint32_t working_entry_count;
static uint32_t last_working_entry_time;

void linux_poweron_handler(void)
{
    working_entry_count++;
    last_working_entry_time = sim.now;
}

// ==================== Локальные заглушки wbec ====================

static bool alarm_enabled;

bool rtc_alarm_is_alarm_enabled(void) { return alarm_enabled; }

bool temperature_control_is_temperature_ready(void) { return true; }
int16_t temperature_control_get_temperature_c_x100(void) { return 2500; }

void usart_tx_buf_blocking(const void * buf, size_t size) { (void)buf; (void)size; }
void buzzer_beep(uint16_t freq, uint16_t duration_ms) { (void)freq; (void)duration_ms; }

// ==================== Модель платы ====================

// Кормление watchdog из "Linux": установка бита reset в регионе WDT
static void soc_feed_watchdog(void)
{
    struct REGMAP_WDT w;
    if (!utest_regmap_get_region_data(REGMAP_REGION_WDT, &w, sizeof(w))) {
        memset(&w, 0, sizeof(w));
        w.timeout = WBEC_WATCHDOG_INITIAL_TIMEOUT_S;
    }
    w.reset = 1;
    regmap_set_region_data(REGMAP_REGION_WDT, &w, sizeof(w));
    utest_regmap_mark_region_changed(REGMAP_REGION_WDT);
}

static void sim_fail(const char * why)
{
    char msg[256];
    snprintf(msg, sizeof(msg),
             "%s: t=%u ms, 5V=%u nRST=%u V33=%u pmic_crashed=%u soc_boots=%u "
             "rail_off=%u warm_pulses=%u nrst_ms=%u not_startable_ms=%u",
             why, sim.now,
             (unsigned)(utest_gpio_get_output_state(gpio_5v) != 0),
             (unsigned)(utest_gpio_get_output_state(gpio_nrst) != 0),
             (unsigned)sim.v33, (unsigned)sim.pmic_crashed, sim.soc_boot_count,
             sim.rail_off_edges, sim.warm_pulses,
             sim.nrst_asserted_ms, sim.not_startable_ms);
    TEST_FAIL_MESSAGE(msg);
}

// Один тик = 1 мс. Порядок вызовов повторяет main loop прошивки:
// wdt_do_periodic_work -> vmon -> linux_cpu_pwr_seq_do_periodic_work -> wbec_do_periodic_work
static void sim_tick(void)
{
    sim.now++;
    utest_systick_advance_time_ms(1);

    // Отложенные события начала тика
    if ((sim.pmic_crash_at != 0) && (sim.now >= sim.pmic_crash_at)) {
        sim.pmic_crash_at = 0;
        sim.pmic_crashed = true;
        sim.v33 = false;    // авария: 3.3В пропадает мгновенно
    }
    if ((sim.powerctrl_at != 0) && (sim.now >= sim.powerctrl_at)) {
        sim.powerctrl_at = 0;
        struct REGMAP_POWER_CTRL p = {
            .off = sim.powerctrl_off ? 1 : 0,
            .reboot = sim.powerctrl_reboot ? 1 : 0,
            .reset_pmic = sim.powerctrl_reset_pmic ? 1 : 0,
        };
        regmap_set_region_data(REGMAP_REGION_POWER_CTRL, &p, sizeof(p));
        utest_regmap_mark_region_changed(REGMAP_REGION_POWER_CTRL);
    }

    bool p5v = utest_gpio_get_output_state(gpio_5v) != 0;
    bool pwron = utest_gpio_get_output_state(gpio_pwron) != 0;
    bool nrst = utest_gpio_get_output_state(gpio_nrst) != 0;

    // Перезапуск зависшего PMIC
    if (sim.pmic_crashed) {
        sim.revive_5v_off_cnt = p5v ? 0 : (sim.revive_5v_off_cnt + 1);
        sim.revive_pwron_cnt = pwron ? (sim.revive_pwron_cnt + 1) : 0;
        if ((sim.revive_5v_off_cnt >= PMIC_REVIVE_5V_OFF_MS) ||
            (sim.revive_pwron_cnt >= PMIC_REVIVE_PWRON_MS))
        {
            sim.pmic_crashed = false;
            sim.revive_5v_off_cnt = 0;
            sim.revive_pwron_cnt = 0;
        }
    }

    // Динамика 3.3В
    bool v33_target = p5v && !sim.pmic_crashed;
#if !defined(WBEC_HAS_WARM_RESET)
    // На WB74 линия PMIC RESET сбрасывает PMIC: выходы PMIC отключаются
    v33_target = v33_target && !nrst;
#endif
    if (v33_target) {
        sim.v33_fall_cnt = 0;
        if (!sim.v33 && (++sim.v33_rise_cnt >= PMIC_START_MS)) {
            sim.v33 = true;
        }
    } else {
        sim.v33_rise_cnt = 0;
        if (sim.v33 && (++sim.v33_fall_cnt >= V33_DECAY_MS)) {
            sim.v33 = false;
        }
    }

    // Модель SoC
    bool soc_startable = p5v && sim.v33 && !nrst;
    if (!soc_startable) {
        sim.soc_run_since = 0;
    } else if (sim.soc_run_since == 0) {
        sim.soc_run_since = sim.now;
        sim.soc_boot_count++;
    } else if (sim.soc_feeds) {
        uint32_t uptime = sim.now - sim.soc_run_since;
        bool hung = (sim.soc_hang_at_uptime_ms != 0) && (uptime >= sim.soc_hang_at_uptime_ms);
        if (!hung && (uptime >= SOC_FEED_UPTIME_MS) &&
            ((uptime - SOC_FEED_UPTIME_MS) % 30000 == 0))
        {
            soc_feed_watchdog();
        }
    }

    // Обновление vmon (модель измерений)
    utest_vmon_set_ready(true);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);
    utest_vmon_set_ch_status(VMON_CHANNEL_V33, sim.v33);
    utest_vmon_set_ch_status(VMON_CHANNEL_V_IN, true);

    // Периодические задачи в порядке main loop
    wdt_do_periodic_work();
    linux_cpu_pwr_seq_do_periodic_work();
    wbec_do_periodic_work();

    // Наблюдения за фронтами (после работы модулей)
    bool p5v_after = utest_gpio_get_output_state(gpio_5v) != 0;
    bool nrst_after = utest_gpio_get_output_state(gpio_nrst) != 0;
    if (sim.prev_p5v && !p5v_after) {
        sim.rail_off_edges++;
    }
    if (sim.prev_nrst && !nrst_after && p5v_after) {
        sim.warm_pulses++;
    }
    sim.prev_p5v = p5v_after;
    sim.prev_nrst = nrst_after;

    if (utest_mcu_get_standby_wakeup_time() != 0) {
        sim.standby_requested = true;
    }

    // Инварианты
    if (sim.check_invariants) {
        sim.nrst_asserted_ms = nrst_after ? (sim.nrst_asserted_ms + 1) : 0;
        if (sim.nrst_asserted_ms > sim.max_nrst_asserted_ms) {
            sim.max_nrst_asserted_ms = sim.nrst_asserted_ms;
        }
        if (sim.nrst_asserted_ms > NRST_ASSERTED_LIMIT_MS) {
            sim_fail("WEDGE: PMIC RESET (PWROK) line is latched, SoC is held in reset");
        }

        if (!sim.standby_requested) {
            bool startable_after = p5v_after && sim.v33 && !nrst_after;
            sim.not_startable_ms = startable_after ? 0 : (sim.not_startable_ms + 1);
            if (sim.not_startable_ms > sim.max_not_startable_ms) {
                sim.max_not_startable_ms = sim.not_startable_ms;
            }
            if (sim.not_startable_ms > NOT_STARTABLE_LIMIT_MS) {
                sim_fail("WEDGE: watchdog did not recover the SoC in time");
            }
        }

        if (!sim.allow_standby && sim.standby_requested) {
            sim_fail("Unexpected standby request");
        }
    }
}

static void sim_run_ms(uint32_t duration_ms)
{
    for (uint32_t i = 0; i < duration_ms; i++) {
        sim_tick();
    }
}

// ==================== Подготовка сценариев ====================

void utest_wbec_reset_state(void);
void utest_linux_power_control_reset_state(void);
void utest_wdt_module_reset_state(void);

void setUp(void)
{
    memset(&sim, 0, sizeof(sim));
    sim.check_invariants = true;
    working_entry_count = 0;
    last_working_entry_time = 0;
    alarm_enabled = false;

    utest_wbec_reset_state();
    utest_linux_power_control_reset_state();
    utest_wdt_module_reset_state();
    utest_gpio_reset_instances();
    utest_mcu_reset();
    utest_systick_set_time_ms(1000);
    utest_vmon_reset();
    utest_regmap_reset();
    utest_system_led_reset();
    utest_wbmz_common_reset();
    utest_pwrkey_reset();
    utest_rtc_reset();
    utest_irq_reset();
}

void tearDown(void)
{
}

// Штатное включение платы до состояния WORKING
static void sim_boot_to_working(void)
{
    utest_mcu_set_poweron_reason(MCU_POWERON_REASON_POWER_ON);
    utest_vmon_set_ready(true);
    utest_vmon_set_ch_status(VMON_CHANNEL_V50, true);

    wbec_init();

    // WAIT_STARTUP -> VOLTAGE_CHECK -> POWER_ON_SEQUENCE_WAIT -> WORKING
    uint32_t guard = 10000;
    while ((working_entry_count == 0) && (guard--)) {
        sim_tick();
    }
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, working_entry_count,
                                  "Board must reach WORKING state during normal boot");
}

// Момент первого срабатывания watchdog после входа в WORKING:
// wdt перезапущен в том же тике, что и вход в WORKING, таймаут наступает
// строго через WBEC_WATCHDOG_INITIAL_TIMEOUT_S * 1000 + 1 мс
static uint32_t expected_wdt_timeout_time(void)
{
    return last_working_entry_time + (WBEC_WATCHDOG_INITIAL_TIMEOUT_S * 1000) + 1;
}

// ==================== Тесты ====================

// Сценарий: здоровая система. Linux загружается и кормит watchdog.
// Ожидание: ни одного сброса за 10 минут, инварианты не нарушены.
static void test_healthy_linux_runs_without_resets(void)
{
    sim.soc_feeds = true;
    sim.soc_hang_at_uptime_ms = 0;

    sim_boot_to_working();
    sim_run_ms(600000);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, sim.soc_boot_count,
                                     "SoC must boot exactly once");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, sim.rail_off_edges,
                                     "5V rail must not be cycled when Linux feeds the watchdog");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, sim.warm_pulses,
                                     "Reset line must not be pulsed when Linux feeds the watchdog");
}

// Сценарий со стенда: Linux зависает через 3.2 с после старта на КАЖДОЙ загрузке,
// успев один раз покормить watchdog (драйвер EC загрузился - система "жива").
// Ожидание: каждый таймаут восстанавливает SoC, клина нет.
static void test_linux_hangs_every_boot_watchdog_always_recovers(void)
{
    sim.soc_feeds = true;
    sim.soc_hang_at_uptime_ms = 3200;

    sim_boot_to_working();
    // ~7 циклов "загрузка - зависание - таймаут" по ~123 c
    sim_run_ms(900000);

    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(5, sim.soc_boot_count,
        "Watchdog must keep restarting the hanging SoC");
#if defined(WBEC_HAS_WARM_RESET)
    // Кормление сбрасывает эскалацию: каждый сброс - тёплый, DRAM сохраняется
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(5, sim.warm_pulses,
        "Every recovery must be a warm reset while feeds arrive between hangs");
#endif
}

// Сценарий: SoC вообще не загружается (не кормит watchdog никогда).
// Ожидание: восстановление продолжается неограниченно долго, клина нет.
// На WB85 эскалация чередуется: тёплый -> жёсткий -> тёплый -> ...
// (после жёсткого сброса начинается новая загрузка, и первое зависание
// в ней снова заслуживает тёплого сброса с сохранением DRAM/ramoops)
static void test_dead_linux_escalation_never_wedges(void)
{
    sim.soc_feeds = false;

    sim_boot_to_working();
    // 4 таймаута: ~500 c
    sim_run_ms(500000);

#if defined(WBEC_HAS_WARM_RESET)
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(2, sim.warm_pulses,
        "Escalation must keep trying warm resets (warm/hard alternation)");
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(1, sim.rail_off_edges,
        "Escalation must reach the hard power cycle stage");
#else
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(3, sim.rail_off_edges,
        "Every watchdog timeout must hard-cycle the power");
#endif
}

// Сценарий-КЛИН (авария со стенда): таймаут watchdog приходит в том же тике,
// в котором пропало 3.3В (авария PMIC).
// До исправления (WB85): обработчик таймаута запускает тёплый сброс
// (линия PMIC RESET (PWROK) взводится), затем в том же проходе ветка контроля
// 3.3В вызывает hard_reset, который затирает состояние PS_WARM_RESET_PULSE.
// Импульс никогда не завершается, линия сброса остаётся взведённой НАВСЕГДА:
// SoC заклинен в сбросе (нет SPL/U-Boot), 5В включено, watchdog "работает",
// но каждый его жёсткий сброс не отпускает линию. Плата мертва до снятия питания.
static void test_wdt_timeout_racing_pmic_crash_does_not_latch_reset_line(void)
{
    sim.soc_feeds = false;

    sim_boot_to_working();
    sim.pmic_crash_at = expected_wdt_timeout_time();

    // 400 c после аварии: восстановление + очередные циклы эскалации
    sim_run_ms((expected_wdt_timeout_time() - sim.now) + 400000);

    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(2, sim.soc_boot_count,
        "SoC must become startable again after the racing PMIC crash");
}

// Сценарий-КЛИН: запрос reset_pmic из Linux приходит в том же тике,
// что и таймаут watchdog.
// До исправления: запрос запускает тёплый сброс (WB85) / сброс PMIC (WB74)
// с взведением линии PMIC RESET (PWROK), а обработчик таймаута в том же
// проходе вызывает hard_reset - линия остаётся взведённой навсегда.
static void test_pmic_reset_request_racing_wdt_timeout_does_not_latch(void)
{
    sim.soc_feeds = false;

    sim_boot_to_working();

#if defined(WBEC_HAS_WARM_RESET)
    // Для жёсткой ветки эскалации нужен второй таймаут без кормления:
    // первый таймаут (тёплый сброс) уже был
    uint32_t first_timeout = expected_wdt_timeout_time();
    sim_run_ms((first_timeout - sim.now) + 1000);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(2, working_entry_count,
        "Board must re-enter WORKING after the first warm reset");
#endif

    sim.powerctrl_at = expected_wdt_timeout_time();
    sim.powerctrl_reset_pmic = true;

    sim_run_ms((expected_wdt_timeout_time() - sim.now) + 400000);

    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(2, sim.soc_boot_count,
        "SoC must become startable again after reset_pmic request racing the timeout");
}

// Сценарий: развёртка момента аварии PMIC вокруг таймаута watchdog.
// Авария попадает во все фазы: до таймаута (штатная ветка потери 3.3В),
// в тот же тик (гонка), во время импульса тёплого сброса, в шаги включения
// PS_ON_STEP*, в паузу жёсткого сброса.
// Ожидание: ни один момент аварии не приводит к клину.
static void test_pmic_crash_timing_sweep_never_wedges(void)
{
    static const int32_t offsets_ms[] = {
        -1500, -1000, -400, -100, -10, -1,
        0,
        1, 10, 50, 99, 100, 101, 150, 400, 600, 1000, 1101, 1500, 2500,
    };

    for (unsigned i = 0; i < sizeof(offsets_ms) / sizeof(offsets_ms[0]); i++) {
        setUp();
        sim.soc_feeds = false;

        sim_boot_to_working();
        sim.pmic_crash_at = expected_wdt_timeout_time() + offsets_ms[i];

        // 300 c после аварии достаточно для восстановления с запасом
        sim_run_ms((sim.pmic_crash_at - sim.now) + 300000);

        char msg[128];
        snprintf(msg, sizeof(msg),
                 "SoC must be restartable after PMIC crash at timeout%+d ms",
                 (int)offsets_ms[i]);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(2, sim.soc_boot_count, msg);
    }
}

// Сценарий: запрос poweroff из Linux (будильник взведён) приходит в том же
// тике, что и таймаут watchdog.
// Ожидание: выключение выигрывает - плата выключается и уходит в standby,
// таймаут не "воскрешает" её.
// До исправления: обработчик таймаута в том же проходе запускал сброс
// поверх начатого выключения, и плата включалась обратно.
static void test_poweroff_request_racing_wdt_timeout_stays_off(void)
{
    sim.soc_feeds = false;
    sim.allow_standby = true;
    alarm_enabled = true;

    sim_boot_to_working();

    sim.powerctrl_at = expected_wdt_timeout_time();
    sim.powerctrl_off = true;

    uint32_t until_req = expected_wdt_timeout_time() - sim.now;
    sim_run_ms(until_req + 2000);

    TEST_ASSERT_TRUE_MESSAGE(sim.standby_requested,
        "Poweroff request must lead to standby even when racing a watchdog timeout");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, utest_gpio_get_output_state(gpio_5v),
        "5V rail must stay off after poweroff, watchdog must not resurrect the board");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, sim.soc_boot_count,
        "SoC must not be restarted after poweroff request");
}

#if defined(WBEC_HAS_WARM_RESET)
// Сценарий: SoC никогда не кормит watchdog - проверка чередования эскалации.
// Ожидание: тёплый -> жёсткий -> тёплый -> жёсткий (каждый жёсткий сброс
// начинает новый цикл загрузки, первое зависание в нём получает тёплый сброс).
static void test_escalation_alternates_warm_and_hard(void)
{
    sim.soc_feeds = false;

    sim_boot_to_working();
    // 4 таймаута: 4 * ~123 c
    sim_run_ms(500000);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, sim.warm_pulses,
        "Timeouts 1 and 3 must be warm resets");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, sim.rail_off_edges,
        "Timeouts 2 and 4 must be hard power cycles");
}
#endif

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_healthy_linux_runs_without_resets);
    RUN_TEST(test_linux_hangs_every_boot_watchdog_always_recovers);
    RUN_TEST(test_dead_linux_escalation_never_wedges);
    RUN_TEST(test_wdt_timeout_racing_pmic_crash_does_not_latch_reset_line);
    RUN_TEST(test_pmic_reset_request_racing_wdt_timeout_does_not_latch);
    RUN_TEST(test_pmic_crash_timing_sweep_never_wedges);
    RUN_TEST(test_poweroff_request_racing_wdt_timeout_stays_off);
#if defined(WBEC_HAS_WARM_RESET)
    RUN_TEST(test_escalation_alternates_warm_and_hard);
#endif

    return UNITY_END();
}
