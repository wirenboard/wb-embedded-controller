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
#include "wdt-stm32.h"

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
    PS_WARM_RESET_PULSE,                // Тёплый сброс SoC: короткий импульс на PMIC_RESET_PWROK
};

struct pwr_ctx {
    // Пробуждение из suspend-to-off: после появления 3.3В нужен
    // импульс на PWROK - PMIC при выходе из сна восстанавливает
    // питание, но не выдаёт сброс, и SoC сам не стартует
    bool wake_pending;
    enum pwr_state state;
    systime_t timestamp;
    unsigned attempt;
    bool initialized;
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

// Подтяжка пина на время Standby (применяется по PWR_CR3_APC). В Standby
// драйверы GPIO выключены и уровень пина задаётся только PWR->PUCRx/PDCRx.
static void standby_pull(const gpio_pin_t * gpio, bool up)
{
    volatile uint32_t * reg;
    if (gpio->port == GPIOA) {
        reg = up ? &PWR->PUCRA : &PWR->PDCRA;
    } else if (gpio->port == GPIOB) {
        reg = up ? &PWR->PUCRB : &PWR->PDCRB;
    } else if (gpio->port == GPIOD) {
        reg = up ? &PWR->PUCRD : &PWR->PDCRD;
    } else {
        return; // GCOVR_EXCL_LINE
    }
    *reg |= (1u << gpio->pin);
}

/**
 * @brief suspend-to-off: усыпляет EC в STM32 Standby на heartbeat-интервал.
 *
 * Отличие от linux_cpu_pwr_seq_off_and_goto_standby: там пин 5В подтягивают
 * ВНИЗ (линукс должен остаться выключенным). Здесь 5В ДОЛЖНО остаться
 * включённым весь сон - PMIC хранит DRAM в self-refresh. Для этого пин 5В
 * (EC_GPIO_LINUX_POWER) НАРОЧНО оставляется ВООБЩЕ БЕЗ подтяжки (high-Z):
 * по схеме WB 8.5.3 ключ 5В модуля (U14 SY6280) держится ВКЛЮЧЁННЫМ внешним
 * резистором R38 470k -> +5V/1; EC выключает его, только активно прижимая EN
 * через R37 10k. C32 2.2 мкФ на EN (tau ~ 1 с) сглаживает и короткие окна
 * сброса при heartbeat-пробуждениях. Никакого раннего перехвата пина или
 * внутренней подтяжки не требуется - 5В держит железо.
 * ВАЖНО: подтяжку ВНИЗ на пин 5В здесь ставить нельзя - это выключит ключ,
 * DRAM выпадет из self-refresh и resume станет невозможен.
 *
 * PWRON и PWROK/RESET при high-Z и так неактивны (NPN-BRT с внутренним
 * резистором база-эмиттер держит транзистор закрытым); подтяжки вниз - только
 * страховка от наводок на длинном окне.
 *
 * Кнопка питания (PA0) уже настроена как WKUP с подтяжкой вверх в pwrkey_init,
 * будильник Alarm A взведён Linux'ом; heartbeat задаёт RTC WUT.
 *
 * @param wakeup_after_s Интервал heartbeat-пробуждения (RTC WUT), НЕ полный
 * дедлайн: обязан быть меньше периода IWDG 10 с (IWDG считает и в Standby,
 * а "кормит" его только перезапуск при сбросе-пробуждении) - см.
 * WBEC_SUSPEND_STANDBY_HEARTBEAT_S в config.h.
 */
void linux_cpu_pwr_seq_suspend_to_standby(uint16_t wakeup_after_s)
{
    standby_pull(&gpio_pmic_pwron, false);
    standby_pull(&gpio_pmic_reset_pwrok, false);

    // Если система живёт от WBMZ (step-up включён, Vin нет), его enable
    // (EC_GPIO_WBMZ_STEPUP_ENABLE) в Standby повиснет и step-up выключится -
    // 5В пропадёт и DRAM умрёт. Держим enable внутренней подтяжкой вверх
    // (~40 кОм; проверить на стенде, что её хватает входу enable на WBMZ).
    if (wbmz_is_stepup_enabled()) {
        static const gpio_pin_t gpio_wbmz_stepup = { EC_GPIO_WBMZ_STEPUP_ENABLE };
        standby_pull(&gpio_wbmz_stepup, true);
    }

    // Apply pull-up and pull-down configuration
    PWR->CR3 |= PWR_CR3_APC;

    mcu_goto_standby(wakeup_after_s);
    // На железе не возвращается: Standby, пробуждение = сброс, разбор в
    // wbec_init(). В юнит-тестах mcu_goto_standby - заглушка и возвращается.
}

/**
 * @brief Инициализация управления питанием при пробуждении из suspend-to-off
 * Standby и взведение последовательности пробуждения PMIC.
 *
 * Пробуждение из Standby - это сброс: подтяжки PWR и режимы GPIO уже в
 * значениях по умолчанию, чистить нечего. 5В всё это время держал внешний R38
 * (пин был high-Z), поэтому берём пин под драйвер без провала уровня: сначала
 * защёлка ODR=1, затем перевод в OUTPUT. Дальше та же последовательность, что
 * и в linux_cpu_pwr_seq_wakeup: ждём появления 3.3В, затем импульс PWROK
 * выводит SoC из сброса (а если 3.3В уже есть - поздний отказ SoC от suspend -
 * последовательность увидит готовое 3.3В и обойдётся без "нажатия" PWRON).
 */
void linux_cpu_pwr_seq_resume_init(void)
{
    linux_cpu_pwr_5v_gpio_on();
    GPIO_S_SET_OUTPUT(gpio_linux_power);

    pmic_pwron_gpio_off();
    pmic_reset_gpio_off();
    GPIO_S_SET_OUTPUT(gpio_pmic_reset_pwrok);
    GPIO_S_SET_OUTPUT(gpio_pmic_pwron);

    pwr_ctx.initialized = true;
    pwr_ctx.wake_pending = true;
    new_state(PS_ON_STEP1_WAIT_3V3);
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

    // Линия PMIC RESET (PWROK) не должна достаться последовательности
    // включения взведённой (например, от прерванного тёплого сброса) -
    // иначе SoC останется заклиненным в сбросе
    pmic_reset_gpio_off();
    linux_cpu_pwr_5v_gpio_on();
    new_state(PS_ON_STEP1_WAIT_3V3);
}

/**
 * @brief Пробуждение PMIC из сна (AXP sleep, режим suspend-to-off).
 * 5В уже включено, 3.3В выключил сам PMIC по команде из BL31.
 * Перезапускаем последовательность включения с шага ожидания 3.3В:
 * если оно не появляется само, штатная эскалация "нажимает" PWRON -
 * для AXP853T в состоянии sleep это источник пробуждения (POK
 * negedge), по которому PMIC восстанавливает записанную конфигурацию.
 * Без сброса состояния: PS_ON_COMPLETE блокирует обычный
 * linux_cpu_pwr_seq_on().
 */
void linux_cpu_pwr_seq_wakeup(void)
{
    pwr_ctx.wake_pending = true;
    pmic_reset_gpio_off();
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
    // Отпускаем линию сброса: никакая последовательность не должна
    // оставлять её взведённой после своего завершения или прерывания
    pmic_reset_gpio_off();
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
    // Отпускаем линию сброса: если жёсткий сброс прервал тёплый сброс
    // или сброс PMIC, линия не должна остаться взведённой - иначе после
    // подачи 5В SoC навсегда останется в сбросе
    pmic_reset_gpio_off();
    new_state(PS_RESET_5V_WAIT);
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
 * @brief Тёплый сброс SoC коротким импульсом на линии PMIC_RESET_PWROK.
 * Линия одновременно заведена на PWROK PMIC и RESET процессора T507.
 * Если в PMIC отключен рестарт по PWROK (AXP REG32[4]=0, значение по
 * умолчанию), PMIC игнорирует импульс и все его выходы, включая питание
 * DRAM, остаются включёнными — сбрасывается только SoC, содержимое DRAM
 * сохраняется (это позволяет ramoops пережить сброс).
 * Если же PMIC настроен на рестарт по PWROK, пропадёт 3.3В и штатная
 * логика включения (PS_ON_STEP1_WAIT_3V3) выполнит полный цикл включения.
 */
void linux_cpu_pwr_seq_warm_reset(void)
{
    pmic_reset_gpio_on();
    new_state(PS_WARM_RESET_PULSE);
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
            watchdog_reload();
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

    switch (pwr_ctx.state) { // GCOVR_EXCL_LINE
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
            if (pwr_ctx.wake_pending) {
                // Питание восстановлено после сна PMIC: SoC ещё в
                // сбросе, толкаем его импульсом на PWROK
                pwr_ctx.wake_pending = false;
                pmic_reset_gpio_on();
                new_state(PS_WARM_RESET_PULSE);
                break;
            }
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
            pmic_pwron_gpio_off();
            if (pwr_ctx.wake_pending) {
                pwr_ctx.wake_pending = false;
                pmic_reset_gpio_on();
                new_state(PS_WARM_RESET_PULSE);
                break;
            }
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

    // Тёплый сброс SoC: отпускаем линию RESET после короткого импульса.
    // Если PMIC проигнорировал импульс, 3.3В на месте и включение
    // завершится сразу; если PMIC перезапустился - штатное включение
    case PS_WARM_RESET_PULSE:
        if (in_state_time_ms() > WBEC_WARM_RESET_PULSE_MS) {
            pmic_reset_gpio_off();
            pmic_pwron_gpio_off();
            // Тёплый сброс всегда заканчивается последовательностью включения.
            // Гарантируем ей 5В: если тёплый сброс был запрошен, когда 5В
            // оказалось снятым, ожидание 3.3В без 5В бессмысленно
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

    default: break; // GCOVR_EXCL_LINE
    }
}

#ifdef __unittest_env__
    #include <string.h>

    void utest_linux_power_control_reset_state(void)
    {
        memset(&pwr_ctx, 0, sizeof(pwr_ctx));
        pwr_ctx.state = PS_INIT_OFF;
    }
#endif
