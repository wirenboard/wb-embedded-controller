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
#include "wbmz-common.h"

static const gpio_pin_t gpio_linux_power = { EC_GPIO_LINUX_POWER };
static const gpio_pin_t gpio_pmic_pwron = { EC_GPIO_LINUX_PMIC_PWRON };
static const gpio_pin_t gpio_pmic_reset_pwrok = { EC_GPIO_LINUX_PMIC_RESET_PWROK };

enum pwr_state {
    PS_INIT_OFF,                        // Выключенное состояние после подачи питания и перед включением линукса

    PS_OFF_COMPLETE,                    // Закончен процесс выключения, переход в standby

    // Включение питания делается максимум за 3 этапа.
    // Если всё идет штатно - то за 1 этап.
    PS_ON_STEP1_WAIT_3V3,               // Ждём, пока появится 3.3В
    PS_ON_STEP2_PMIC_PWRON,             // Если 3.3В не появляется, пробуем включить PMIC "нажатием" на PWRON
    PS_ON_STEP3_PMIC_PWRON_OFF_WAIT,    // Если и это не помогло, отпускаем PWRON и делаем несколько попыток
    PS_ON_COMPLETE,

    PS_RESET_5V_WAIT,                   // Нужно при перезаргрузке - выключаем 5В и ждём разрядку линий
    PS_RESET_PMIC_WAIT,                 // Сброс PMIC через PMIC_RESET_PWROK. Ждём, пока пропадёт 3.3В
};

struct pwr_ctx {
    enum pwr_state state;
    systime_t timestamp;
    unsigned attempt;
    bool initialized;
    bool reset_flag;
};

static struct pwr_ctx pwr_ctx = {
    .state = PS_INIT_OFF,
};

static inline void linux_cpu_pwr_5v_gpio_on(void)   { GPIO_S_SET(gpio_linux_power); }
static inline void linux_cpu_pwr_5v_gpio_off(void)  { GPIO_S_RESET(gpio_linux_power); }
static inline void pmic_pwron_gpio_on(void)         { GPIO_S_SET(gpio_pmic_pwron); }
static inline void pmic_pwron_gpio_off(void)        { GPIO_S_RESET(gpio_pmic_pwron); }
static inline void pmic_reset_gpio_on(void)         { GPIO_S_SET(gpio_pmic_reset_pwrok); }
static inline void pmic_reset_gpio_off(void)        { GPIO_S_RESET(gpio_pmic_reset_pwrok); }

static inline void new_state(enum pwr_state s)
{
    pwr_ctx.state = s;
    pwr_ctx.timestamp = systick_get_system_time_ms();
}

static inline systime_t in_state_time_ms(void)
{
    return systick_get_time_since_timestamp(pwr_ctx.timestamp);
}


static void goto_standby_and_save_5v_status(void)
{
    if (vmon_get_ch_status(VMON_CHANNEL_V50)) {
        console_print_w_prefix("5V line status: voltage present\r\n");
        mcu_save_vcc_5v_last_state(MCU_VCC_5V_STATE_ON);
    } else {
        console_print_w_prefix("5V line status: no voltage\r\n");
        mcu_save_vcc_5v_last_state(MCU_VCC_5V_STATE_OFF);
    }
    console_print_w_prefix("Power off and go to standby now\r\n");
    linux_cpu_pwr_seq_off_and_goto_standby(WBEC_PERIODIC_WAKEUP_FIRST_TIMEOUT_S);
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
void linux_cpu_pwr_seq_init(bool on)
{
    if (on) {
        linux_cpu_pwr_seq_on();
    } else {
        linux_cpu_pwr_5v_gpio_off();
        new_state(PS_INIT_OFF);
    }
    GPIO_S_SET_OUTPUT(gpio_linux_power);

    pmic_pwron_gpio_off();
    pmic_reset_gpio_off();
    GPIO_S_SET_OUTPUT(gpio_pmic_reset_pwrok);
    GPIO_S_SET_OUTPUT(gpio_pmic_pwron);

    pwr_ctx.initialized = true;
}

/**
 * @brief Переводит EC в standby и заводит таймер на пробуждение.
 * Питание линукса держится выключенным в этом режиме.
 *
 * @param wakeup_after_s Задержка на пробуждение в секундах
 */
void linux_cpu_pwr_seq_off_and_goto_standby(uint16_t wakeup_after_s)
{
    // Подтяжка вниз в режиме standby для GPIO управления питанием линукса
    // Таким образом, в standby линукс будет выключен
    PWR->PDCRD |= (1 << gpio_linux_power.pin);

    // Apply pull-up and pull-down configuration
    PWR->CR3 |= PWR_CR3_APC;

    mcu_goto_standby(wakeup_after_s);
}

/**
 * @brief Включает питание линукс штатным способом:
 * Включается 5В, затем контролируется появление 3.3В.
 * Если 3.3В не появляется, выполняется 3 попытки включить PMIC через PWRON
 */
void linux_cpu_pwr_seq_on(void)
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
 * @brief Выключение питания путём отключения 5В сразу, без PMIC.
 * Нужно для отключения по долгому нажатию
 */
void linux_cpu_pwr_seq_hard_off(void)
{
    linux_cpu_pwr_5v_gpio_off();
    pmic_pwron_gpio_off();
    new_state(PS_OFF_COMPLETE);
}

/**
 * @brief Сброс питания (выключение и через 1с включение)
 * Через отключение 5В (без PMIC)
 */
void linux_cpu_pwr_seq_hard_reset()
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
void linux_cpu_pwr_seq_reset_pmic(void)
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
bool linux_cpu_pwr_seq_is_busy(void)
{
    return (
        (pwr_ctx.state != PS_OFF_COMPLETE) &&
        (pwr_ctx.state != PS_ON_COMPLETE)
    );
}

void linux_cpu_pwr_seq_do_periodic_work(void)
{
    if (!vmon_ready() || !pwr_ctx.initialized) {
        return;
    }

    if (pwrkey_handle_long_press()) {
        linux_cpu_pwr_5v_gpio_off();
        console_print("\r\n\n");
        console_print_w_prefix("Power off after power key long press detected.\r\n");
        system_led_disable();
        wbmz_disable_stepup();
        // Ждём отпускания кнопки
        while (pwrkey_pressed()) {
            pwrkey_do_periodic_work();
        }
        goto_standby_and_save_5v_status();
    }

    // Если неожиданно пропало питание +5В,
    // это означает, что разрядился WBMZ, а EC продолжает работать от BATSENSE
    // или Vin < 9V или выдернули USB (WBMZ при этом не был включен)
    // В общем случае - не важно почему +5В пропало. Нужно перейти в спящий режим
    if (!vmon_get_ch_status(VMON_CHANNEL_V50)) {
        console_print_w_prefix("Voltage on 5V line is lost, power off and go to standby now\r\n");
        goto_standby_and_save_5v_status();
    }

    switch (pwr_ctx.state) {
    // Если алгоритм ещё не начался - ничего не делаем
    case PS_INIT_OFF:
        break;

    case PS_ON_COMPLETE:
        wbmz_do_periodic_work();
        break;

    case PS_OFF_COMPLETE:
        // Если алгоритм выключил питание - нужно отключить WBMZ и проверить,
        // осталось ли напряжение на +5В. Это может быть USB или Vin < 11.5V
        if (wbmz_is_stepup_enabled()) {
            wbmz_disable_stepup();
        }
        if (in_state_time_ms() > 200) {
            goto_standby_and_save_5v_status();
        }
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

    default:
        break;
    }
}
