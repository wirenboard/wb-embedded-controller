#include "utest_pwrkey.h"
#include "config.h"
#include "systick.h"

static bool pwrkey_long_press = false;
static bool pwrkey_short_press = false;
static bool pwrkey_is_pressed = false;
static bool pwrkey_is_ready = true;
static uint32_t pwrkey_periodic_work_call_count = 0;

// --- Опциональная модель антидребезга (зеркалит src/pwrkey.c) ---
// По умолчанию выключена: pwrkey_do_periodic_work() остаётся no-op-счётчиком,
// как раньше, чтобы не менять поведение существующих тестов.
enum dbnc_state {
    DBNC_UNINIT = 0,
    DBNC_RELEASED,
    DBNC_PRESSED,
};

static bool dbnc_model_enabled = false;
static enum dbnc_state dbnc_prev_raw;        // предыдущее сырое состояние
static systime_t       dbnc_raw_change_ts;   // когда сырое состояние изменилось
static enum dbnc_state dbnc_logic;           // состояние после антидребезга
static enum dbnc_state dbnc_prev_logic;      // для детектирования фронтов
static bool            dbnc_press_begun;     // было начато нажатие

void utest_pwrkey_reset(void)
{
    pwrkey_long_press = false;
    pwrkey_short_press = false;
    pwrkey_is_pressed = false;
    pwrkey_is_ready = true;
    pwrkey_periodic_work_call_count = 0;

    dbnc_model_enabled = false;
    dbnc_prev_raw = DBNC_UNINIT;
    dbnc_raw_change_ts = 0;
    dbnc_logic = DBNC_UNINIT;
    dbnc_prev_logic = DBNC_UNINIT;
    dbnc_press_begun = false;
}

void utest_set_pwrkey_long_press(bool value)
{
    pwrkey_long_press = value;
}

void utest_set_pwrkey_pressed(bool value)
{
    pwrkey_is_pressed = value;
}

void utest_pwrkey_set_ready(bool ready)
{
    pwrkey_is_ready = ready;
}

void utest_pwrkey_set_short_press(bool value)
{
    pwrkey_short_press = value;
}

uint32_t utest_pwrkey_get_periodic_work_call_count(void)
{
    return pwrkey_periodic_work_call_count;
}

void utest_pwrkey_enable_debounce_model(bool on)
{
    dbnc_model_enabled = on;
}

bool pwrkey_handle_long_press(void)
{
    bool ret = pwrkey_long_press;
    pwrkey_long_press = false;
    return ret;
}

bool pwrkey_handle_short_press(void)
{
    bool ret = pwrkey_short_press;
    pwrkey_short_press = false;
    return ret;
}

bool pwrkey_ready(void)
{
    return pwrkey_is_ready;
}

bool pwrkey_pressed(void)
{
    // На железе pwrkey_pressed() возвращает АНТИДРЕБЕЗЖЕННЫЙ уровень
    // (gpio_ctx.logic_state), а не сырой GPIO. С включённой моделью зеркалим
    // это: уровень меняется через PWRKEY_DEBOUNCE_MS после сырого, и «короткое
    // нажатие» фиксируется в том же проходе, в котором уровень становится
    // released - инвариант, на который опирается «глотание» хвоста нажатия
    // после окна сна. Раньше мок отдавал сырой уровень (mock gap).
    if (dbnc_model_enabled) {
        return dbnc_logic == DBNC_PRESSED;
    }
    return pwrkey_is_pressed;
}

static void dbnc_do_periodic_work(void)
{
    enum dbnc_state raw = pwrkey_is_pressed ? DBNC_PRESSED : DBNC_RELEASED;

    // Антидребезг: raw -> logic после > PWRKEY_DEBOUNCE_MS стабильности.
    if (dbnc_prev_raw != raw) {
        dbnc_prev_raw = raw;
        dbnc_raw_change_ts = systick_get_system_time_ms();
    }
    if (dbnc_logic != raw) {
        if (systick_get_time_since_timestamp(dbnc_raw_change_ts) > PWRKEY_DEBOUNCE_MS) {
            dbnc_logic = raw;
        }
    }

    // Детектирование нажатий по подтверждённому logic-состоянию.
    if (dbnc_prev_logic != dbnc_logic) {
        if (dbnc_prev_logic == DBNC_RELEASED && dbnc_logic == DBNC_PRESSED) {
            dbnc_press_begun = true;
        }
        if (dbnc_prev_logic == DBNC_PRESSED && dbnc_logic == DBNC_RELEASED) {
            if (dbnc_press_begun) {
                pwrkey_short_press = true;   // подтверждённое короткое нажатие
            }
            dbnc_press_begun = false;
        }
        dbnc_prev_logic = dbnc_logic;
    }
}

void pwrkey_do_periodic_work(void)
{
    pwrkey_periodic_work_call_count++;
    if (dbnc_model_enabled) {
        dbnc_do_periodic_work();
    }
}
