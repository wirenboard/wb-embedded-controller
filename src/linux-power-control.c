#include "linux-power-control.h"
#include "config.h"
#include "gpio.h"
#include "systick.h"
#include "voltage-monitor.h"

static const gpio_pin_t gpio_linux_power = { EC_GPIO_LINUX_POWER };
static const gpio_pin_t gpio_pmic_pwron = { EC_GPIO_LINUX_PMIC_PWRON };
static const gpio_pin_t gpio_pmic_reset_pwrok = { EC_GPIO_LINUX_PMIC_RESET_PWROK };
static const gpio_pin_t gpio_usb_console = { EC_GPIO_USB_CONSOLE_PWR_EN };
static const gpio_pin_t gpio_usb_network = { EC_GPIO_USB_NETWORK_PWR_EN };

enum pwr_state {
    PS_OFF_COMPLETE,
    PS_OFF_STEP1_PMIC_PWRON,

    PS_ON_COMPLETE,
    PS_ON_STEP1_WAIT_3V3,
    PS_ON_STEP2_PMIC_PWRON,
    PS_ON_STEP3_PMIC_PWRON_WAIT,

    PS_RESET_5V_WAIT,
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
    bool do_reset;
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
static inline void usb_console_gpio_on(void)     { GPIO_S_SET(gpio_usb_console); }
static inline void usb_console_gpio_off(void)    { GPIO_S_RESET(gpio_usb_console); }
static inline void usb_network_gpio_on(void)     { GPIO_S_SET(gpio_usb_network); }
static inline void usb_network_gpio_off(void)    { GPIO_S_RESET(gpio_usb_network); }

static inline void new_state(enum pwr_state s)
{
    pwr_ctx.state = s;
    pwr_ctx.timestamp = systick_get_system_time_ms();
}

static inline systime_t in_state_time(void)
{
    return systick_get_time_since_timestamp(pwr_ctx.timestamp);
}

static inline void usb_power_control(void)
{
    // Питание от портов USB выполнено через диоды, которые могут быть зашунтированы ключами
    // для снижения падения напряжений.
    // Проблема при таком решении в том, что открый ключ пропускает ток в обе стороны
    // и при подключении обоих USB нельзя понять, когда один из них отключится
    // и выбрать нужный USB для питания
    // Поэтому, если напряжение есть на обоих USB, то ключи закрываем и работаем через диоды
    if (vmon_get_ch_status(VMON_CHANNEL_V_IN)) {
        // Если есть входное напряжение - работает от него, ключи на USB закрыты
        if (pwr_ctx.power_src != POWER_SRC_V_IN) {
            pwr_ctx.power_src = POWER_SRC_V_IN;
            usb_console_gpio_off();
            usb_network_gpio_off();
        }
    } else if (vmon_get_ch_status(VMON_CHANNEL_VBUS_DEBUG) && !vmon_get_ch_status(VMON_CHANNEL_VBUS_NETWORK)) {
        if (pwr_ctx.power_src != POWER_SRC_USB_CONSOLE) {
            pwr_ctx.power_src = POWER_SRC_USB_CONSOLE;
            usb_console_gpio_on();
            usb_network_gpio_off();
        }
    } else if (!vmon_get_ch_status(VMON_CHANNEL_VBUS_DEBUG) && vmon_get_ch_status(VMON_CHANNEL_VBUS_NETWORK)) {
        if (pwr_ctx.power_src != POWER_SRC_USB_NETWORK) {
            pwr_ctx.power_src = POWER_SRC_USB_NETWORK;
            usb_console_gpio_off();
            usb_network_gpio_on();
        }
    } else {
        if (pwr_ctx.power_src != POWER_SRC_BOTH_USB) {
            pwr_ctx.power_src = POWER_SRC_BOTH_USB;
            usb_console_gpio_off();
            usb_network_gpio_off();
        }
    }
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

    usb_console_gpio_off();
    usb_network_gpio_off();
    GPIO_S_SET_OUTPUT(gpio_usb_console);
    GPIO_S_SET_OUTPUT(gpio_usb_network);
}

/**
 * @brief Выключает питание линукс штатным способом:
 * Активируется сигнал PWRON на PMIC, ждём выключения PMIC, потом выключаем 5В
 */
void linux_pwr_off(void)
{
    pmic_pwron_gpio_on();
    new_state(PS_OFF_STEP1_PMIC_PWRON);
    pwr_ctx.do_reset = 0;
}

/**
 * @brief Сбрасывает питание линукс штатным способом через PMIC.
 * Сначала активируем PWRON, ждём выключения PMIC, потом сбрасываем 5В
 * и включаем питание заново
 */
void linux_pwr_reset(void)
{
    pmic_pwron_gpio_on();
    new_state(PS_OFF_STEP1_PMIC_PWRON);
    pwr_ctx.do_reset = 1;
}

/**
 * @brief Сбрасывает питание через отключение 5В сразу, без участия PMIC.
 * Нужно, чтобы сбросить питание, например, при выходе реек питания за пределы.
 * В этот момент PMIC уже может не функционировать нормально.
 */
void linux_pwr_hard_reset(void)
{
    linux_pwr_gpio_off();
    new_state(PS_RESET_5V_WAIT);
}

/**
 * @brief Выключение питания путём отключения 5В сразу, без PMIC.
 * Нужно для отключения по долгому нажатию
 */
void linux_pwr_hard_off(void)
{
    linux_pwr_gpio_off();
    new_state(PS_OFF_COMPLETE);
}

/**
 * @brief Статус работы алгоритма управления питанием
 *
 * @return true Питание включено или выключено, алгоритм завершён
 * @return false Алгоритм что-то делает, питанием в неопределенном состоянии
 */
bool linux_pwr_is_busy(void)
{
    return (
        (pwr_ctx.state != PS_OFF_COMPLETE) &&
        (pwr_ctx.state != PS_ON_COMPLETE)
    );
}

void linux_pwr_do_periodic_work(void)
{
    if (!vmon_ready()) {
        return;
    }

    usb_power_control();

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
                linux_pwr_gpio_off();
                new_state(PS_RESET_5V_WAIT);
            }
        }
        break;

    // Третий шаг включения - отпускаем PWRON, ждём, пробуем ещё раз
    case PS_ON_STEP3_PMIC_PWRON_WAIT:
        if (in_state_time() > 500) {
            pmic_pwron_gpio_on();
            new_state(PS_ON_STEP2_PMIC_PWRON);
        }
        break;

    // Сброс питания 5В
    case PS_RESET_5V_WAIT:
        if (in_state_time() > 1000) {
            linux_pwr_gpio_on();
            new_state(PS_ON_STEP1_WAIT_3V3);
        }
        break;

    // Выключение питания штатным путём через PWRON
    // В этом состоянии PWRON активирован
    case PS_OFF_STEP1_PMIC_PWRON:
        if ((!vmon_get_ch_status(VMON_CHANNEL_V33)) ||
            (in_state_time() > 7000))
        {
            // Если пропало 3.3В (или не пропало и случился таймаут)
            // выключаем 5В
            pmic_pwron_gpio_off();
            linux_pwr_off();
            // И переходим либо в выключенное состояние, либо в ресет
            if (pwr_ctx.do_reset) {
                new_state(PS_RESET_5V_WAIT);
            } else {
                new_state(PS_OFF_COMPLETE);
            }
        }
        break;

    default:
        break;
    }
}
