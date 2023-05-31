#include "linux-power-control.h"
#include "config.h"
#include "gpio.h"
#include "systick.h"
#include "voltage-monitor.h"
#include "usart_tx.h"
#include "pwrkey.h"
#include "mcu-pwr.h"
#include "adc.h"

static const gpio_pin_t gpio_linux_power = { EC_GPIO_LINUX_POWER };
static const gpio_pin_t gpio_pmic_pwron = { EC_GPIO_LINUX_PMIC_PWRON };
static const gpio_pin_t gpio_pmic_reset_pwrok = { EC_GPIO_LINUX_PMIC_RESET_PWROK };
static const gpio_pin_t gpio_wbmz_status_bat = { EC_GPIO_WBMZ_STATUS_BAT };
static const gpio_pin_t gpio_wbmz_on = { EC_GPIO_WBMZ_ON };

enum pwr_state {
    PS_OFF_COMPLETE,
    PS_OFF_STEP1_PMIC_PWRON,

    PS_ON_COMPLETE,
    PS_ON_STEP1_WAIT_3V3,
    PS_ON_STEP2_PMIC_PWRON,
    PS_ON_STEP3_PMIC_PWRON_WAIT,

    PS_RESET_5V_WAIT,
    PS_RESET_PMIC_WAIT,

    PS_LONG_PRESS_HANDLE,
};

enum power_source {
    POWER_SRC_V_IN,
    POWER_SRC_USB_CONSOLE,
    POWER_SRC_USB_NETWORK,
    POWER_SRC_BOTH_USB,
};

struct pwr_ctx {
    enum pwr_state state;
    enum power_source power_src;
    systime_t timestamp;
    unsigned attempt;
    bool wbmz_enabled;
    bool initialized;
};

static struct pwr_ctx pwr_ctx = {
    .state = PS_OFF_COMPLETE,
    .power_src = POWER_SRC_V_IN,
};

static inline void linux_pwr_gpio_on(void)       { GPIO_S_SET(gpio_linux_power); }
static inline void linux_pwr_gpio_off(void)      { GPIO_S_RESET(gpio_linux_power); }
static inline void pmic_pwron_gpio_on(void)      { GPIO_S_SET(gpio_pmic_pwron); }
static inline void pmic_pwron_gpio_off(void)     { GPIO_S_RESET(gpio_pmic_pwron); }
static inline void pmic_reset_gpio_on(void)      { GPIO_S_SET(gpio_pmic_reset_pwrok); }
static inline void pmic_reset_gpio_off(void)     { GPIO_S_RESET(gpio_pmic_reset_pwrok); }
static inline void wbmz_off(void)                { GPIO_S_RESET(gpio_wbmz_on); }
static inline void wbmz_on(void)                 { GPIO_S_SET(gpio_wbmz_on); }
static inline bool wbmz_working(void)            { return (!GPIO_S_TEST(gpio_wbmz_status_bat)); }

static inline void new_state(enum pwr_state s)
{
    pwr_ctx.state = s;
    pwr_ctx.timestamp = systick_get_system_time_ms();
}

static inline systime_t in_state_time(void)
{
    return systick_get_time_since_timestamp(pwr_ctx.timestamp);
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
        linux_pwr_gpio_on();
        new_state(PS_ON_STEP1_WAIT_3V3);
    } else {
        linux_pwr_gpio_off();
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
    // WBMZ OFF - включение/выключение WBMZ
    pwr_ctx.wbmz_enabled = 0;
    wbmz_off();
    GPIO_S_SET_OUTPUT(gpio_wbmz_on);

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
        (pwr_ctx.state == PS_ON_STEP3_PMIC_PWRON_WAIT))
    {
        return;
    }

    linux_pwr_gpio_on();
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
}

/**
 * @brief Выключение питания путём отключения 5В сразу, без PMIC.
 * Нужно для отключения по долгому нажатию
 */
void linux_pwr_hard_off(void)
{
    linux_pwr_gpio_off();
    pmic_pwron_gpio_off();
    new_state(PS_OFF_COMPLETE);
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
    wbmz_on();
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

    if (pwrkey_handle_long_press()) {
        if (vmon_get_ch_status(VMON_CHANNEL_V33)) {
            pmic_pwron_gpio_on();
            new_state(PS_LONG_PRESS_HANDLE);
        }
    }

    // Управление питанием WBMZ
    // Если мы тут находимся, значит хотим линукс включить
    // И должны включить WBMZ, если:
    // 1. Vin > 11.5V
    // 2. WBMZ выключен и Vin стало < 9V
    // 3. WBMZ выключен и 5В куда-то пропало (работаем от USB и вытащили его)
    // Выключать WBMZ специально не надо - оно выключится само при переходе в standby
    // При этом ЕС продолжит работать от BATSENSE
    uint16_t vin = adc_get_ch_mv(ADC_CHANNEL_ADC_V_IN);
    bool vbus_present = vmon_get_ch_status(VMON_CHANNEL_VBUS_DEBUG) || vmon_get_ch_status(VMON_CHANNEL_VBUS_NETWORK);

    if (!pwr_ctx.wbmz_enabled) {
        if (vin > 11500) {
            usart_tx_str_blocking("\r\nVin > 11500; WBMZ ON\r\n");
            linux_pwr_enable_wbmz();
        }
        else if ((vin < 9000) && (!vbus_present)) {
            usart_tx_str_blocking("\r\nVin < 9000; no Vbus; WBMZ ON\r\n");
            linux_pwr_enable_wbmz();
        }
    }

    switch (pwr_ctx.state) {
    // Если алгоритм завершился (включил или выключил питание)
    // ничего не делаем
    case PS_OFF_COMPLETE:
    case PS_ON_COMPLETE:
        break;

    // Первый шаг включения питания: проверка, что 3.3В появилось, после того как подали 5В
    case PS_ON_STEP1_WAIT_3V3:
        if (vmon_get_ch_status(VMON_CHANNEL_V33)) {
            // Если 3.3В появилось, то считаем что питание включено
            new_state(PS_ON_COMPLETE);
        }
        if (in_state_time() > 500) {
            // Если 3.3В не появилось, то попробуем включить PMIC через PWRON
            usart_tx_str_blocking("No voltage on 3.3V line, try to switch on PMIC throught PWRON\n");
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
        if (in_state_time() > 1500) {
            pwr_ctx.attempt++;
            pmic_pwron_gpio_off();
            if (pwr_ctx.attempt <= 3) {
                // Если попытки не исчерпаны - отключаем PWRON и пробуем ещё
                new_state(PS_ON_STEP3_PMIC_PWRON_WAIT);
            } else {
                // Если попытки кончились - сбрасываем 5В и начинаем заново
                usart_tx_str_blocking("Still no voltage on 3.3V line, reset 5V line and try to switch on again\n");
                linux_pwr_gpio_off();
                new_state(PS_RESET_5V_WAIT);
            }
        }
        break;

    // Третий шаг включения - отпускаем PWRON, ждём, пробуем ещё раз
    case PS_ON_STEP3_PMIC_PWRON_WAIT:
        if (in_state_time() > 500) {
            usart_tx_str_blocking("One more attempt to switch on PMIC throught PWRON\n");
            pmic_pwron_gpio_on();
            new_state(PS_ON_STEP2_PMIC_PWRON);
        }
        break;

    // Сброс питания 5В
    case PS_RESET_5V_WAIT:
        if (in_state_time() > WBEC_POWER_RESET_TIME_MS) {
            linux_pwr_gpio_on();
            new_state(PS_ON_STEP1_WAIT_3V3);
        }
        break;

    // Сброс PMIC через RESET самого PMIC
    case PS_RESET_PMIC_WAIT:
        if ((!vmon_get_ch_status(VMON_CHANNEL_V33)) || (in_state_time() > 2000)) {
            usart_tx_str_blocking("PMIC was reset throught RESET line\n");
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
            usart_tx_str_blocking("PMIC switched off throught PWRON, disabling 5V line now\n");
            pmic_pwron_gpio_off();
            linux_pwr_off();
            new_state(PS_OFF_COMPLETE);
        } else if (in_state_time() > 7000) {
            usart_tx_str_blocking("Warning: PMIC not switched off throught PWRON after 7s, disabling 5V line now\n");
            pmic_pwron_gpio_off();
            linux_pwr_off();
            new_state(PS_OFF_COMPLETE);
        }
        break;

    // В это состояние переходим после детектирования долгого нажатия
    // PMIC_PWRON активирован
    case PS_LONG_PRESS_HANDLE:
        if ((!vmon_get_ch_status(VMON_CHANNEL_V33)) ||
            (in_state_time() > 10000))
        {
            // Если пропало 3.3В или вышел таймаут - выключаем 5В и засыпаем
            pmic_pwron_gpio_off();
            pmic_reset_gpio_off();
            linux_pwr_gpio_off();
            new_state(PS_OFF_COMPLETE);
            usart_tx_str_blocking("\n\rPower off after power key long press detected.\r\n\n");
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
