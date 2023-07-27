#include "linux-power-control.h"
#include "config.h"
#include "gpio.h"
#include "systick.h"
#include "voltage-monitor.h"
#include "usart_tx.h"
#include "pwrkey.h"
#include "mcu-pwr.h"
#include "adc.h"
#include "system-led.h"
#include "console.h"
#include "regmap-int.h"

static const gpio_pin_t gpio_linux_power = { EC_GPIO_LINUX_POWER };
static const gpio_pin_t gpio_pmic_pwron = { EC_GPIO_LINUX_PMIC_PWRON };
static const gpio_pin_t gpio_pmic_reset_pwrok = { EC_GPIO_LINUX_PMIC_RESET_PWROK };
static const gpio_pin_t gpio_wbmz_status_bat = { EC_GPIO_WBMZ_STATUS_BAT };
static const gpio_pin_t gpio_wbmz_on = { EC_GPIO_WBMZ_ON };

enum pwr_state {
    // Выключение питания делается за 1 этап:
    PS_OFF_STEP1_PMIC_PWRON,            // Активируем линию PMIC_PWRON и ждём, пока пропадёт 3.3В (примерно 6с)
    PS_OFF_COMPLETE,                    // Питание выключено

    // Включение питания делается максимум за 3 этапа.
    // Если всё идет штатно - то за 1 этап.
    PS_ON_STEP1_WAIT_3V3,               // Ждём, пока появится 3.3В
    PS_ON_STEP2_PMIC_PWRON,             // Если 3.3В не появляется, пробуем включить PMIC "нажатием" на PWRON
    PS_ON_STEP3_PMIC_PWRON_OFF_WAIT,    // Если и это не помогло, отпускаем PWRON и делаем несколько попыток
    PS_ON_COMPLETE,

    PS_RESET_5V_WAIT,                   // Нужно при перезаргрузке - выключаем 5В и ждём разрядку линий
    PS_RESET_PMIC_WAIT,                 // Сброс PMIC через PMIC_RESET_PWROK. Ждём, пока пропадёт 3.3В

    PS_LONG_PRESS_HANDLE,               // Трансляция долгого нажатия в PMIC_PWRON и ожидание выключения PMIC
};

struct pwr_ctx {
    enum pwr_state state;
    systime_t timestamp;
    unsigned attempt;
    bool wbmz_enabled;
    bool initialized;
    bool reset_flag;
};

static struct pwr_ctx pwr_ctx = {
    .state = PS_OFF_COMPLETE,
};

static inline void linux_cpu_pwr_5v_gpio_on(void)   { GPIO_S_SET(gpio_linux_power); }
static inline void linux_cpu_pwr_5v_gpio_off(void)  { GPIO_S_RESET(gpio_linux_power); }
static inline void pmic_pwron_gpio_on(void)         { GPIO_S_SET(gpio_pmic_pwron); }
static inline void pmic_pwron_gpio_off(void)        { GPIO_S_RESET(gpio_pmic_pwron); }
static inline void pmic_reset_gpio_on(void)         { GPIO_S_SET(gpio_pmic_reset_pwrok); }
static inline void pmic_reset_gpio_off(void)        { GPIO_S_RESET(gpio_pmic_reset_pwrok); }
static inline void wbmz_on(void)                    { GPIO_S_SET(gpio_wbmz_on); }
static inline bool wbmz_working(void)               { return (!GPIO_S_TEST(gpio_wbmz_status_bat)); }

static inline void new_state(enum pwr_state s)
{
    pwr_ctx.state = s;
    pwr_ctx.timestamp = systick_get_system_time_ms();
}

static inline systime_t in_state_time_ms(void)
{
    return systick_get_time_since_timestamp(pwr_ctx.timestamp);
}

static void put_power_status_to_regmap(void)
{
    struct REGMAP_PWR_STATUS p = {};
    p.powered_from_wbmz = linux_pwr_is_powered_from_wbmz();
    p.wbmz_enabled = pwr_ctx.wbmz_enabled;

    regmap_set_region_data(REGMAP_REGION_PWR_STATUS, &p, sizeof(p));
}

/**
 * @brief Инициализирует GPIO управления питанием как выходы.
 * При включении питания WB питание на процессор не подается, пока не зарядится RC-цепочка
 * на ключе питания.
 * Функция может как подхватить выключенное состояние и продлить его,
 * так и сразу подать питание на процессорный модуль
 *
 * @param on Начальное состояние питания (вкл/выкл)
 */
void linux_pwr_init(bool on)
{
    if (on) {
        linux_pwr_on();
    } else {
        linux_cpu_pwr_5v_gpio_off();
        new_state(PS_OFF_COMPLETE);
    }
    GPIO_S_SET_OUTPUT(gpio_linux_power);

    pmic_pwron_gpio_off();
    pmic_reset_gpio_off();
    GPIO_S_SET_OUTPUT(gpio_pmic_reset_pwrok);
    GPIO_S_SET_OUTPUT(gpio_pmic_pwron);

    // Подтяжка вниз в режиме standby для GPIO управления питанием линукса
    // Таким образом, в standby линукс будет выключен
    PWR->PDCRD |= (1 << gpio_linux_power.pin);

    // STATUS_BAT это вход, который WBMZ тянет к земле открытым коллектором
    // Подтянут снаружи к V_EC
    GPIO_S_SET_INPUT(gpio_wbmz_status_bat);

    pwr_ctx.initialized = true;
}

/**
 * @brief Включает питание линукс штатным способом:
 * Включается 5В, затем контролируется появление 3.3В.
 * Если 3.3В не появляется, выполняется 3 попытки включить PMIC через PWRON
 */
void linux_pwr_on(void)
{
    if ((pwr_ctx.state == PS_ON_COMPLETE) ||
        (pwr_ctx.state == PS_ON_STEP1_WAIT_3V3) ||
        (pwr_ctx.state == PS_ON_STEP2_PMIC_PWRON) ||
        (pwr_ctx.state == PS_ON_STEP3_PMIC_PWRON_OFF_WAIT))
    {
        return;
    }

    linux_cpu_pwr_5v_gpio_on();
    new_state(PS_ON_STEP1_WAIT_3V3);
}

/**
 * @brief Выключает питание линукс штатным способом:
 * Активируется сигнал PWRON на PMIC, ждём выключения PMIC, потом выключаем 5В
 */
void linux_pwr_off(void)
{
    if ((pwr_ctx.state == PS_OFF_COMPLETE) ||
        (pwr_ctx.state == PS_OFF_STEP1_PMIC_PWRON))
    {
        return;
    }

    pmic_pwron_gpio_on();
    new_state(PS_OFF_STEP1_PMIC_PWRON);
    pwr_ctx.reset_flag = false;
}

/**
 * @brief Выключение питания путём отключения 5В сразу, без PMIC.
 * Нужно для отключения по долгому нажатию
 */
void linux_pwr_hard_off(void)
{
    linux_cpu_pwr_5v_gpio_off();
    pmic_pwron_gpio_off();
    new_state(PS_OFF_COMPLETE);
}

/**
 * @brief Сброс питания (выключение и через 1с включение)
 * Через PMIC PWRON (как штатное выключение-включение)
 */
void linux_pwr_reset()
{
    linux_pwr_off();
    pwr_ctx.reset_flag = true;
}

/**
 * @brief Сброс питания (выключение и через 1с включение)
 * Через отключение 5В (без PMIC)
 */
void linux_pwr_hard_reset()
{
    linux_cpu_pwr_5v_gpio_off();
    pmic_pwron_gpio_off();
    new_state(PS_RESET_5V_WAIT);
    pwr_ctx.reset_flag = true;
}

/**
 * @brief Сброс PMIC через линию RESET.
 * В нормальной работе не используется, нужен для проверки схемотехники и монтажа.
 * Возможно как-то понадобится в последствии.
 */
void linux_pwr_reset_pmic(void)
{
    pmic_reset_gpio_on();
    new_state(PS_RESET_PMIC_WAIT);
}

/**
 * @brief Статус работы алгоритма управления питанием
 *
 * @return true Питание включено или выключено, алгоритм завершён
 * @return false Алгоритм что-то делает, питание в неопределенном состоянии
 */
bool linux_pwr_is_busy(void)
{
    return (
        (pwr_ctx.state != PS_OFF_COMPLETE) &&
        (pwr_ctx.state != PS_ON_COMPLETE)
    );
}

void linux_pwr_enable_wbmz(void)
{
    // Вызывается только один раз либо в wbec_init, либо в linux_pwr_do_periodic_work
    // Однажны включившийся WBMZ более не выключается
    wbmz_on();
    GPIO_S_SET_OUTPUT(gpio_wbmz_on);
    pwr_ctx.wbmz_enabled = 1;
}

bool linux_pwr_is_powered_from_wbmz(void)
{
    return wbmz_working();
}

void linux_pwr_do_periodic_work(void)
{
    if (!vmon_ready() || !pwr_ctx.initialized) {
        return;
    }

    put_power_status_to_regmap();

    if (pwrkey_handle_long_press()) {
        pmic_pwron_gpio_on();
        new_state(PS_LONG_PRESS_HANDLE);
    }

    // Управление питанием WBMZ
    // Если мы тут находимся, значит хотим линукс включить
    // И должны включить WBMZ, если Vin > 11.5V
    // Выключать WBMZ специально не надо - оно выключится само при переходе в standby
    // При этом ЕС продолжит работать от BATSENSE
    if (!pwr_ctx.wbmz_enabled) {
        if (adc_get_ch_mv(ADC_CHANNEL_ADC_V_IN) > 11500) {
            linux_pwr_enable_wbmz();
        }
    }

    // Если неожиданно пропало питание +5В,
    // это означает, что разрядился WBMZ, а EC продолжает работать от BATSENSE
    // или Vin < 9V или выдернули USB (WBMZ при этом не был включен)
    // В общем случае - не важно почему +5В пропало. Нужно перейти в спящий режим
    if (!vmon_get_ch_status(VMON_CHANNEL_V50)) {
        console_print_w_prefix("No 5V, power off and go to standby now\r\n");
        mcu_save_vcc_5v_last_state(MCU_VCC_5V_STATE_OFF);
        mcu_goto_standby(WBEC_PERIODIC_WAKEUP_FIRST_TIMEOUT_S);
    }

    switch (pwr_ctx.state) {
    // Если алгоритм завершился (выключил или включил питание) - ничего не делаем
    case PS_OFF_COMPLETE:
    case PS_ON_COMPLETE:
        break;

    // Первый шаг включения питания: проверка, что 3.3В появилось, после того как подали 5В
    case PS_ON_STEP1_WAIT_3V3:
        if (vmon_get_ch_status(VMON_CHANNEL_V33)) {
            // Если 3.3В появилось, то считаем что питание включено
            new_state(PS_ON_COMPLETE);
        }
        if (in_state_time_ms() > 1000) {
            // Если 3.3В не появилось, то попробуем включить PMIC через PWRON
            console_print_w_prefix("No voltage on 3.3V line, try to switch on PMIC throught PWRON\r\n");
            pmic_pwron_gpio_on();
            pwr_ctx.attempt = 0;
            new_state(PS_ON_STEP2_PMIC_PWRON);
        }
        break;

    // Второй шаг включения питания: активирован PWRON, ждем появления 3.3В
    // или выходим по таймауту
    // Это не штатный режим и сюда попадать по идее не должны
    // PMIC должен включаться сам после подачи 5В
    case PS_ON_STEP2_PMIC_PWRON:
        if (vmon_get_ch_status(VMON_CHANNEL_V33)) {
            // Если 3.3В
            pmic_pwron_gpio_off();
            new_state(PS_ON_COMPLETE);
        }
        if (in_state_time_ms() > 1500) {
            pwr_ctx.attempt++;
            pmic_pwron_gpio_off();
            if (pwr_ctx.attempt <= 3) {
                // Если попытки не исчерпаны - отключаем PWRON и пробуем ещё
                new_state(PS_ON_STEP3_PMIC_PWRON_OFF_WAIT);
            } else {
                // Если попытки кончились - сбрасываем 5В и начинаем заново
                console_print_w_prefix("Still no voltage on 3.3V line, reset 5V line and try to switch on again\r\n");
                // Выключаем линию 5В на время WBEC_POWER_RESET_TIME_MS
                linux_cpu_pwr_5v_gpio_off();
                new_state(PS_RESET_5V_WAIT);
            }
        }
        break;

    // Третий шаг включения - отпускаем PWRON, ждём, пробуем ещё раз
    case PS_ON_STEP3_PMIC_PWRON_OFF_WAIT:
        if (in_state_time_ms() > 500) {
            console_print_w_prefix("One more attempt to switch on PMIC throught PWRON\r\n");
            pmic_pwron_gpio_on();
            new_state(PS_ON_STEP2_PMIC_PWRON);
        }
        break;

    // Сброс питания 5В
    case PS_RESET_5V_WAIT:
        if (in_state_time_ms() > WBEC_POWER_RESET_TIME_MS) {
            linux_cpu_pwr_5v_gpio_on();
            new_state(PS_ON_STEP1_WAIT_3V3);
        }
        break;

    // Сброс PMIC через RESET самого PMIC
    case PS_RESET_PMIC_WAIT:
        if ((!vmon_get_ch_status(VMON_CHANNEL_V33)) || (in_state_time_ms() > 2000)) {
            console_print_w_prefix("PMIC was reset throught RESET line\r\n");
            pmic_reset_gpio_off();
            pmic_pwron_gpio_off();
            new_state(PS_ON_STEP1_WAIT_3V3);
        }
        break;

    // Выключение питания штатным путём через PWRON
    // В этом состоянии PWRON активирован
    case PS_OFF_STEP1_PMIC_PWRON:
        // Если пропало 3.3В (или не пропало и случился таймаут)
        // выключаем 5В
        // И переходим либо в выключенное состояние
        if (!vmon_get_ch_status(VMON_CHANNEL_V33)) {
            console_print_w_prefix("PMIC switched off throught PWRON, disabling 5V line now\r\n");
            pmic_pwron_gpio_off();
            linux_cpu_pwr_5v_gpio_off();
            if (pwr_ctx.reset_flag) {
                new_state(PS_RESET_5V_WAIT);
            } else {
                new_state(PS_OFF_COMPLETE);
            }
        } else if (in_state_time_ms() > 8000) {
            console_print_w_prefix("Warning: PMIC not switched off throught PWRON after 8s, disabling 5V line now\r\n");
            pmic_pwron_gpio_off();
            linux_cpu_pwr_5v_gpio_off();
            if (pwr_ctx.reset_flag) {
                new_state(PS_RESET_5V_WAIT);
            } else {
                new_state(PS_OFF_COMPLETE);
            }
        }
        break;

    // В это состояние переходим после детектирования долгого нажатия
    // PMIC_PWRON активирован
    case PS_LONG_PRESS_HANDLE:
        if ((!vmon_get_ch_status(VMON_CHANNEL_V33)) ||
            (in_state_time_ms() > 10000))
        {
            // Если пропало 3.3В или вышел таймаут - выключаем 5В и засыпаем
            pmic_pwron_gpio_off();
            pmic_reset_gpio_off();
            linux_cpu_pwr_5v_gpio_off();
            new_state(PS_OFF_COMPLETE);
            console_print("\r\n\n");
            console_print_w_prefix("Power off after power key long press detected.\r\n\n");
            system_led_disable();
            // Ждём отпускания кнопки
            while (pwrkey_pressed()) {
                pwrkey_do_periodic_work();
            }
            if (linux_pwr_is_powered_from_wbmz()) {
                mcu_save_vcc_5v_last_state(MCU_VCC_5V_STATE_OFF);
            } else {
                mcu_save_vcc_5v_last_state(MCU_VCC_5V_STATE_ON);
            }
            mcu_goto_standby(WBEC_PERIODIC_WAKEUP_FIRST_TIMEOUT_S);
        } else if (!pwrkey_pressed()) {
            // Если кнопку отпустили - отпускаем PMIC_PWRON
            pmic_pwron_gpio_off();
            new_state(PS_ON_STEP1_WAIT_3V3);
        }
        break;

    default:
        break;
    }
}
