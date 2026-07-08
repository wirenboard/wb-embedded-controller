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
#include "utest_wdt_stm32.h"

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
static bool alarm_fired;

bool rtc_alarm_is_alarm_enabled(void) { return alarm_enabled; }

bool rtc_alarm_take_fired(void)
{
    bool ret = alarm_fired;
    alarm_fired = false;
    return ret;
}

bool temperature_control_is_temperature_ready(void) { return true; }
int16_t temperature_control_get_temperature_c_x100(void) { return 2500; }

// Управляемые супер-циклом выходы, переводимые в безопасное состояние на окно
// suspend-to-off. Отслеживаем текущее состояние заморозки для проверок.
static bool heater_suspend_off;
static bool v_out_suspend_off;

void temperature_control_suspend(bool on) { heater_suspend_off = on; }
void gpio_suspend(bool on) { v_out_suspend_off = on; }

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

    // Периодические задачи в порядке main loop. pwrkey_do_periodic_work идёт
    // первым, как в main.c: с включённой моделью антидребезга (по умолчанию
    // выключена) он превращает удержание кнопки в подтверждённое короткое
    // нажатие по системному времени — так же, как на железе.
    pwrkey_do_periodic_work();
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
bool utest_wbec_get_suspend_started(void);
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
    heater_suspend_off = false;
    v_out_suspend_off = false;
}

// Причины включения из wbec.c (порядок enum linux_poweron_reason)
enum utest_wbec_poweron_reason {
    UTEST_REASON_POWER_ON,
    UTEST_REASON_POWER_KEY,
    UTEST_REASON_RTC_ALARM,
    UTEST_REASON_REBOOT,
    UTEST_REASON_REBOOT_NO_ALARM,
    UTEST_REASON_WATCHDOG,
    UTEST_REASON_PMIC_OFF,
    UTEST_REASON_UNKNOWN,
    UTEST_REASON_WATCHDOG_WARM,
};

// poweron_reason пишется в regmap на каждом wbec_do_periodic_work.
static uint16_t get_poweron_reason_from_regmap(void)
{
    struct REGMAP_POWERON_REASON pr;
    TEST_ASSERT_TRUE(utest_regmap_get_region_data(REGMAP_REGION_POWERON_REASON, &pr, sizeof(pr)));
    return pr.poweron_reason;
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

// Объявление окна suspend-to-off из Linux через регион SUSPEND_CTRL.
static void sim_announce_off_mode(uint16_t timeout_s)
{
    struct REGMAP_SUSPEND_CTRL s = { .timeout_s = timeout_s, .off_mode = 1 };
    regmap_set_region_data(REGMAP_REGION_SUSPEND_CTRL, &s, sizeof(s));
    utest_regmap_mark_region_changed(REGMAP_REGION_SUSPEND_CTRL);
}

// Регрессия (rtcwake -m mem): устаревшая защёлка будильника, оставшаяся от
// прошлого пробуждения (ALRAF в домене резервного питания + программный
// alarm_fired_latch), НЕ должна будить плату сразу при входе в off-mode.
// До фикста первый же опрос off-mode читал это как свежее срабатывание и
// будил плату через ~220 мс вместо запрошенного времени сна.
static void test_suspend_off_stale_alarm_does_not_wake_immediately(void)
{
    sim.check_invariants = false;   // off-mode: плата намеренно "не стартует"
    sim.soc_feeds = true;
    sim_boot_to_working();

    uint32_t boots_before = sim.soc_boot_count;

    // Устаревшее событие будильника от прошлого пробуждения
    alarm_fired = true;

    // Linux объявляет окно suspend-to-off (заведомо длинный дедлайн)
    sim_announce_off_mode(3600);
    sim_tick();     // вход в off-mode должен дренажировать защёлку

    TEST_ASSERT_FALSE_MESSAGE(alarm_fired,
        "Off-mode entry must drain the stale software alarm-fired latch");
    TEST_ASSERT_TRUE_MESSAGE(utest_rtc_was_alarm_flag_cleared(),
        "Off-mode entry must clear the hardware ALRAF flag");

    // BL31 гасит 3.3В (в модели - как зависший PMIC: 3.3В пропало и не
    // вернётся, пока EC не перезапустит PMIC пробуждением)
    sim.pmic_crashed = true;

    // 5 секунд сна - без свежего будильника плата НЕ должна проснуться
    sim_run_ms(5000);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(boots_before, sim.soc_boot_count,
        "Stale alarm latch must NOT wake the board immediately");
}

// Регрессия: свежий будильник, сработавший уже во время off-mode, обязан
// разбудить плату (тем же путём linux_cpu_pwr_seq_wakeup) - фикс не должен
// подавлять реальное пробуждение по будильнику.
static void test_suspend_off_fresh_alarm_wakes(void)
{
    sim.check_invariants = false;
    sim.soc_feeds = true;
    sim_boot_to_working();

    uint32_t boots_before = sim.soc_boot_count;

    sim_announce_off_mode(3600);
    sim_tick();
    sim.pmic_crashed = true;    // BL31 снял 3.3В
    sim_run_ms(2000);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(boots_before, sim.soc_boot_count,
        "Board must be asleep before a fresh alarm fires");

    // Свежий будильник; PMIC при пробуждении восстанавливает 3.3В
    alarm_fired = true;
    sim.pmic_crashed = false;
    sim_run_ms(3000);           // время на последовательность пробуждения

    TEST_ASSERT_TRUE_MESSAGE(sim.soc_boot_count > boots_before,
        "A fresh off-mode alarm must wake the board");
}

// Один цикл off-mode: объявление, пропадание 3.3В (сон начался),
// пробуждение по свежему будильнику (resume того же ядра), возврат в WORKING.
static void sim_off_mode_cycle(void)
{
    sim_announce_off_mode(60);
    sim_tick();
    sim.pmic_crashed = true;         // BL31 снял 3.3В -> arm взводится
    sim_run_ms(500);
    alarm_fired = true;              // будильник будит
    sim.pmic_crashed = false;
    sim_run_ms(3000);                // последовательность пробуждения -> WORKING
}

// Регрессия (fail-without-fix): после выхода из off-mode по будильнику (resume
// того же ядра - ЕС не перезапускается) флаг «сон начался» / разрешение выхода
// из suspend по кормлению watchdog ОБЯЗАН вернуться в исходное (disarmed)
// состояние. Иначе на следующем цикле первое же кормление watchdog завершит
// suspend немедленно (на стенде: RF2+ [EC] Suspend mode: off (request)).
static void test_suspend_off_exit_disarms_feed_exit(void)
{
    sim.check_invariants = false;
    sim.soc_feeds = true;
    sim_boot_to_working();

    sim_announce_off_mode(60);
    sim_tick();
    sim.pmic_crashed = true;          // 3.3В пропало -> arm взводится
    sim_run_ms(500);
    TEST_ASSERT_TRUE_MESSAGE(utest_wbec_get_suspend_started(),
        "feed-exit arm must be set once the EC has seen the 3.3V drop");

    alarm_fired = true;               // будильник будит, resume того же ядра
    sim.pmic_crashed = false;
    sim_run_ms(3000);

    TEST_ASSERT_FALSE_MESSAGE(utest_wbec_get_suspend_started(),
        "suspend-mode exit must disarm the feed-exit / seen-3.3V-drop flag");
}

// Регрессия многоцикловая: цикл 1 взводит arm и выходит по будильнику; на
// цикле 2 (watchdog работает и кормит во время входа) плата обязана так же
// уснуть и проснуться по будильнику, а не выйти из suspend по кормлению.
static void test_suspend_off_second_cycle_sleeps_with_watchdog(void)
{
    sim.check_invariants = false;
    sim.soc_feeds = true;
    sim_boot_to_working();

    sim_off_mode_cycle();                       // цикл 1
    uint32_t boots_after_c1 = sim.soc_boot_count;

    // Цикл 2: объявляем и кормим watchdog во время входа (до и после 3.3В)
    sim_announce_off_mode(60);
    sim_tick();
    soc_feed_watchdog();
    sim_run_ms(150);
    sim.pmic_crashed = true;                    // 3.3В пропало
    sim_run_ms(300);
    soc_feed_watchdog();                        // кормление при взведённом arm
    sim_run_ms(1000);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(boots_after_c1, sim.soc_boot_count,
        "cycle 2 must stay asleep, not exit suspend on a watchdog feed");

    alarm_fired = true;                         // свежий будильник
    sim.pmic_crashed = false;
    sim_run_ms(3000);
    TEST_ASSERT_TRUE_MESSAGE(sim.soc_boot_count > boots_after_c1,
        "cycle 2 must still wake on a fresh alarm");
}

// Регрессия (стенд 2026-07-08, подозрение «залипший ALRAF»): два окна подряд,
// первое завершается СРАБОТАВШИМ будильником. Второе окно НЕ должно мгновенно
// классифицировать будильник на входе — защёлки прошлого цикла обязаны быть
// дренированы объявлением; будит только свежий будильник, и причина
// пробуждения — RTC_ALARM свежего события.
static void test_suspend_off_second_window_no_stale_alarm_at_entry(void)
{
    sim.check_invariants = false;
    sim.soc_feeds = true;
    sim_boot_to_working();

    sim_off_mode_cycle();                       // окно 1: будильник будит
    uint32_t boots_after_c1 = sim.soc_boot_count;

    // Окно 2 (запрос 60 с; дедлайн 60+10 с — далеко за границей проверки)
    sim_announce_off_mode(60);
    sim_tick();
    sim.pmic_crashed = true;                    // BL31 снял 3.3В
    sim_run_ms(30000);                          // полминуты сна

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(boots_after_c1, sim.soc_boot_count,
        "Window 2 must NOT wake instantly on a stale alarm from window 1");

    alarm_fired = true;                         // свежий будильник
    sim.pmic_crashed = false;
    sim_run_ms(3000);

    TEST_ASSERT_TRUE_MESSAGE(sim.soc_boot_count > boots_after_c1,
        "Window 2 must wake on the fresh alarm");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(UTEST_REASON_RTC_ALARM, get_poweron_reason_from_regmap(),
        "Fresh-alarm wake must report REASON_RTC_ALARM");
}

// Фиксация поведения (стенд 2026-07-08): будильник, сработавший МЕЖДУ
// объявлением окна и реальным пропаданием 3.3В, обязан разбудить плату
// НЕМЕДЛЕННО при входе в окно — запрошенное время пробуждения уже прошло.
// На стенде это выглядело как «мгновенное пробуждение»: диагностический вход
// BL31 в suspend занимает ~80 с (подсчёт контрольных сумм DRAM), и запросы
// 30-60 с истекают ещё ДО начала сна. Это корректное поведение, унаследованное
// от busy-poll a655128; тест защищает его от «исправления».
static void test_suspend_off_alarm_fired_before_sleep_start_wakes_at_entry(void)
{
    sim.check_invariants = false;
    sim.soc_feeds = true;
    sim_boot_to_working();
    uint32_t boots_before = sim.soc_boot_count;

    sim_announce_off_mode(30);
    sim_tick();                     // объявление дренирует УСТАРЕВШИЕ защёлки

    // Будильник срабатывает, пока 3.3В ещё присутствует (SoC долго готовится
    // ко сну) — плата НЕ должна перезапускаться, пока сон не начался
    sim_run_ms(2000);
    alarm_fired = true;
    sim_run_ms(1000);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(boots_before, sim.soc_boot_count,
        "Board must not restart while 3.3V is still up");

    // 3.3В наконец пропало: окно открывается с уже сработавшим будильником
    sim.pmic_crashed = true;
    sim_run_ms(8000);               // мгновенная классификация + пробуждение

    TEST_ASSERT_TRUE_MESSAGE(sim.soc_boot_count > boots_before,
        "Entry with an already-fired alarm must wake the board immediately");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(UTEST_REASON_RTC_ALARM, get_poweron_reason_from_regmap(),
        "Pre-sleep-fired alarm must be reported as REASON_RTC_ALARM");
}

// ==================== Stop 1: окно сна suspend-to-off ====================

// Доводит плату до состояния «спит в окне suspend-to-off»: объявлен off-mode,
// 3.3В пропало (BL31), EC ушёл в STM32 Stop. Инварианты выключены: плата
// намеренно не стартует, пока не разбудят.
static void sim_enter_off_mode_sleep(uint16_t timeout_s)
{
    sim.check_invariants = false;
    sim.soc_feeds = true;
    sim_boot_to_working();
    // Как на реальной плате, suspend объявляется, когда Linux уже загружен:
    // до этого момента короткое нажатие кнопки трактуется как немедленное
    // выключение (ветка !linux_booted), а не как выход из suspend.
    sim_run_ms(WBEC_LINUX_BOOT_TIME_MS + 1000);
    sim_announce_off_mode(timeout_s);
    sim_tick();
    sim.pmic_crashed = true;         // BL31 снял 3.3В -> взводится suspend_started
    sim_run_ms(500);
}

// Вход в окно должен: завести источники пробуждения Stop, арм-нуть WUT ровно
// с периодом кормления, принудительно выключить нагреватель (PD2) и V_OUT
// (PD0), реально уснуть и НИ разу не уйти в Standby. Свежий будильник (запрос
// Linux) обязан разбудить тем же путём linux_cpu_pwr_seq_wakeup, вернуть
// выходы под управление, снять маску SoC-CS и выключить WUT-дедлайн.
static void test_stop_window_arms_safe_and_wakes_on_fresh_alarm(void)
{
    sim_enter_off_mode_sleep(60);

    TEST_ASSERT_TRUE_MESSAGE(utest_wbec_get_suspend_started(),
        "3.3V drop must arm the sleep");
    TEST_ASSERT_TRUE_MESSAGE(utest_mcu_stop_get_window_prepared(),
        "Stop window wake sources (RTC/​button EXTI) must be armed");
    uint16_t wut_period = 0;
    TEST_ASSERT_TRUE_MESSAGE(utest_rtc_get_periodic_wakeup_set(&wut_period),
        "the feed/​deadline WUT must be armed");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(WBEC_SUSPEND_STOP_FEED_PERIOD_S, wut_period,
        "WUT must be armed with the feed period");
    TEST_ASSERT_TRUE_MESSAGE(heater_suspend_off, "heater (PD2) must be forced off at Stop entry");
    TEST_ASSERT_TRUE_MESSAGE(v_out_suspend_off, "V_OUT (PD0) must be forced off at Stop entry");
    TEST_ASSERT_TRUE_MESSAGE(utest_mcu_stop_get_enter_count() > 0,
        "EC must actually enter Stop while asleep");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, utest_mcu_get_standby_wakeup_time(),
        "the Stop window must never request Standby");

    uint32_t boots_before = sim.soc_boot_count;

    alarm_fired = true;              // свежий будильник; PMIC восстановит 3.3В
    sim.pmic_crashed = false;
    sim_run_ms(3000);

    TEST_ASSERT_TRUE_MESSAGE(sim.soc_boot_count > boots_before,
        "a fresh alarm must wake the board");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(UTEST_REASON_RTC_ALARM, get_poweron_reason_from_regmap(),
        "alarm wake must report REASON_RTC_ALARM");
    TEST_ASSERT_TRUE_MESSAGE(utest_mcu_stop_get_window_finished(),
        "real wake must restore the SoC-CS EXTI mask (window finish)");
    TEST_ASSERT_TRUE_MESSAGE(utest_rtc_get_periodic_wakeup_disabled(),
        "real wake must disable the WUT deadline");
    TEST_ASSERT_FALSE_MESSAGE(heater_suspend_off, "heater control must be restored on wake");
    TEST_ASSERT_FALSE_MESSAGE(v_out_suspend_off, "V_OUT control must be restored on wake");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, utest_mcu_get_standby_wakeup_time(),
        "no Standby must be requested across the whole window");
}

// Кнопка PWRON: фронт EXTI0 будит ядро, но короткое нажатие подтверждается
// штатным антидребезгом (500 мс) в бодрствующем супер-цикле, а не самим
// фронтом — так сохраняется фильтр случайных касаний. Подтверждённое нажатие
// будит плату с REASON_POWER_KEY.
//
// Нажатие прогоняется END-TO-END через реальный фильтр 500 мс: раз включена
// модель антидребезга, удержание кнопки (utest_set_pwrkey_pressed) превращается
// в короткое нажатие только после > PWRKEY_DEBOUNCE_MS удержания и последующего
// отпускания, по системному времени через pwrkey_do_periodic_work — а не
// инъекцией готового short-press на границе мока.
static void test_stop_wake_by_button_edge_then_debounced_press(void)
{
    // Модель антидребезга включена с самого начала — как на железе, где pwrkey
    // крутится всё время и удерживает базовое состояние logic=RELEASED.
    utest_pwrkey_enable_debounce_model(true);
    sim_enter_off_mode_sleep(60);
    uint32_t boots_before = sim.soc_boot_count;

    // Фронт кнопки будит Stop; одновременно кнопка физически удерживается.
    utest_mcu_stop_set_button_wake(true);
    utest_set_pwrkey_pressed(true);
    // Держим дольше антидребезга: нажатие подтверждается (press begin), но
    // короткое нажатие ещё НЕ зафиксировано (нужно отпускание). Плата не будится.
    sim_run_ms(PWRKEY_DEBOUNCE_MS + 50);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(boots_before, sim.soc_boot_count,
        "a held-but-not-released press must not wake the board yet");

    // Отпускание; PMIC при пробуждении восстановит 3.3В. Через > PWRKEY_DEBOUNCE_MS
    // антидребезг подтверждает отпускание -> короткое нажатие (всё ещё в пределах
    // окна ожидания debounce+grace) -> плата будится.
    utest_set_pwrkey_pressed(false);
    sim.pmic_crashed = false;
    sim_run_ms(PWRKEY_DEBOUNCE_MS + 3000);

    TEST_ASSERT_TRUE_MESSAGE(sim.soc_boot_count > boots_before,
        "a debounced short press must wake the board");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(UTEST_REASON_POWER_KEY, get_poweron_reason_from_regmap(),
        "button wake must report REASON_POWER_KEY");
}

// Регрессия (стенд 2026-07-08, кнопочный тест): одно нажатие = пробуждение +
// poweroff + cold boot. Пока кнопку держат в окне сна, обычный обработчик
// WORKING (linux_booted -> irq_set_flag(IRQ_PWR_OFF_REQ)) выставляет Linux'у
// «запрос выключения»; Linux спит и флаг не квитирует, и после resume драйвер
// wbec-pwrkey доставлял то же нажатие второй раз - logind выключал только что
// разбуженную плату. Нажатие, потреблённое как причина пробуждения, НЕ должно
// оставлять Linux'у отложенных событий кнопки.
static void test_stop_button_wake_press_not_redelivered_to_linux(void)
{
    utest_pwrkey_enable_debounce_model(true);
    sim_enter_off_mode_sleep(60);
    uint32_t boots_before = sim.soc_boot_count;

    // Фронт кнопки будит Stop; кнопка физически удерживается.
    utest_mcu_stop_set_button_wake(true);
    utest_set_pwrkey_pressed(true);
    sim_run_ms(PWRKEY_DEBOUNCE_MS + 50);

    // Предусловие бага реально воспроизведено: пока кнопка удерживалась,
    // обычный обработчик успел выставить запрос выключения спящему Linux.
    TEST_ASSERT_TRUE_MESSAGE(utest_irq_is_flag_set(IRQ_PWR_OFF_REQ),
        "precondition: the running-state handler must latch IRQ_PWR_OFF_REQ while held");

    // Отпускание -> короткое нажатие -> пробуждение.
    utest_set_pwrkey_pressed(false);
    sim.pmic_crashed = false;
    sim_run_ms(PWRKEY_DEBOUNCE_MS + 3000);

    TEST_ASSERT_TRUE_MESSAGE(sim.soc_boot_count > boots_before,
        "the debounced short press must wake the board");
    TEST_ASSERT_FALSE_MESSAGE(utest_irq_is_flag_set(IRQ_PWR_OFF_REQ),
        "the wake press must NOT stay pending for Linux as a power-key event");
}

// Контроль от перекоррекции: нажатие в ОБЫЧНОЙ работе (Linux загружен, suspend
// не объявлен) по-прежнему доставляется в Linux как запрос выключения.
static void test_running_press_still_delivers_pwr_off_req(void)
{
    utest_pwrkey_enable_debounce_model(true);
    sim.soc_feeds = true;
    sim_boot_to_working();
    sim_run_ms(WBEC_LINUX_BOOT_TIME_MS + 1000);     // linux_booted = true

    utest_set_pwrkey_pressed(true);
    sim_run_ms(PWRKEY_DEBOUNCE_MS + 100);

    TEST_ASSERT_TRUE_MESSAGE(utest_irq_is_flag_set(IRQ_PWR_OFF_REQ),
        "a press during normal running must still be delivered to Linux");

    utest_set_pwrkey_pressed(false);
    sim_run_ms(PWRKEY_DEBOUNCE_MS + 100);
}

// Отрицательный случай: касание, которое так и не прошло антидребезг, после
// окна ожидания должно вернуть EC в Stop (а не разбудить плату).
static void test_stop_button_brush_reenters_stop(void)
{
    sim_enter_off_mode_sleep(60);
    uint32_t boots_before = sim.soc_boot_count;

    utest_mcu_stop_set_button_wake(true);
    sim_run_ms(50);
    uint32_t enters_awake = utest_mcu_stop_get_enter_count();

    // Ждём дольше окна ожидания (debounce + grace) без подтверждённого нажатия.
    sim_run_ms(PWRKEY_DEBOUNCE_MS + WBEC_SUSPEND_STOP_BUTTON_GRACE_MS + 200);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(boots_before, sim.soc_boot_count,
        "a button brush that never debounces must not wake the board");
    TEST_ASSERT_TRUE_MESSAGE(utest_mcu_stop_get_enter_count() > enters_awake,
        "after the grace window the EC must go back to sleep (re-enter Stop)");
}

// Дедлайн-бэкстоп: считается по накопленным тикам RTC WUT (systick в Stop
// заморожен). После timeout_ms / (feed_period*1000) тиков WUT плата будится с
// REASON_WATCHDOG. Регрессия: раньше дедлайн жил на systick, который в Stop
// не идёт.
static void test_stop_deadline_fires_via_wut_ticks(void)
{
    sim_enter_off_mode_sleep(60);       // timeout = 60 c + 10 c запас = 70 c

    // Каждый уход в Stop моделирует один истёкший период WUT.
    utest_mcu_stop_set_auto_wut_tick(true);
    sim_run_ms(500);                    // >> 70000/4000 = 18 тиков; PMIC не оживляем

    TEST_ASSERT_FALSE_MESSAGE(utest_wbec_get_suspend_started(),
        "the WUT-tick deadline must end the suspend window");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(UTEST_REASON_WATCHDOG, get_poweron_reason_from_regmap(),
        "deadline wake must report REASON_WATCHDOG");
    TEST_ASSERT_TRUE_MESSAGE(utest_mcu_stop_get_window_finished(),
        "deadline wake must finish the Stop window");
    TEST_ASSERT_TRUE_MESSAGE(utest_rtc_get_periodic_wakeup_disabled(),
        "deadline wake must disable the WUT");
}

// Зеркальная регрессия: сам по себе идущий systick (без тиков WUT) НЕ должен
// сработать дедлайн — иначе доказательства, что база времени переехала на WUT,
// нет. Плата обязана спать сколь угодно долго.
static void test_stop_frozen_wut_never_fires_on_systick_alone(void)
{
    sim_enter_off_mode_sleep(60);
    uint32_t boots_before = sim.soc_boot_count;
    uint32_t enters_before = utest_mcu_stop_get_enter_count();

    // auto_wut_tick выключен: тиков WUT нет, но сим-время (systick) идёт.
    sim_run_ms(120000);                 // сильно больше timeout (70 c)

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(boots_before, sim.soc_boot_count,
        "a frozen WUT accumulator must never fire the deadline on systick alone");
    TEST_ASSERT_TRUE_MESSAGE(utest_wbec_get_suspend_started(),
        "board must still be asleep in the Stop window");
    TEST_ASSERT_TRUE_MESSAGE(utest_mcu_stop_get_enter_count() > enters_before,
        "the EC must keep sleeping in Stop across the window");
}

// Инвариант IWDG (прямая регрессия на причину отказа Standby): аппаратный
// watchdog кормится не реже одного раза на каждый уход в Stop (E1, перед сном),
// поэтому за окно сна он никогда не досчитает до ~10 с.
static void test_stop_feeds_iwdg_on_every_feed_tick(void)
{
    sim_enter_off_mode_sleep(60);

    uint32_t reloads_before = utest_watchdog_get_reload_count();
    uint32_t enters_before = utest_mcu_stop_get_enter_count();
    sim_run_ms(1000);
    uint32_t reloads = utest_watchdog_get_reload_count() - reloads_before;
    uint32_t enters = utest_mcu_stop_get_enter_count() - enters_before;

    TEST_ASSERT_TRUE_MESSAGE(enters > 0, "EC must keep entering Stop");
    TEST_ASSERT_TRUE_MESSAGE(reloads >= enters,
        "the IWDG must be fed at least once per Stop feed-tick");
}

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
    RUN_TEST(test_suspend_off_stale_alarm_does_not_wake_immediately);
    RUN_TEST(test_suspend_off_fresh_alarm_wakes);
    RUN_TEST(test_suspend_off_exit_disarms_feed_exit);
    RUN_TEST(test_suspend_off_second_cycle_sleeps_with_watchdog);
    RUN_TEST(test_suspend_off_second_window_no_stale_alarm_at_entry);
    RUN_TEST(test_suspend_off_alarm_fired_before_sleep_start_wakes_at_entry);

    // Stop 1: окно сна suspend-to-off
    RUN_TEST(test_stop_window_arms_safe_and_wakes_on_fresh_alarm);
    RUN_TEST(test_stop_wake_by_button_edge_then_debounced_press);
    RUN_TEST(test_stop_button_wake_press_not_redelivered_to_linux);
    RUN_TEST(test_running_press_still_delivers_pwr_off_req);
    RUN_TEST(test_stop_button_brush_reenters_stop);
    RUN_TEST(test_stop_deadline_fires_via_wut_ticks);
    RUN_TEST(test_stop_frozen_wut_never_fires_on_systick_alone);
    RUN_TEST(test_stop_feeds_iwdg_on_every_feed_tick);
#if defined(WBEC_HAS_WARM_RESET)
    RUN_TEST(test_escalation_alternates_warm_and_hard);
#endif

    return UNITY_END();
}
