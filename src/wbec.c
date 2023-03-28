#include "config.h"
#include "gpio.h"
#include "regmap-int.h"
#include "pwrkey.h"
#include "irq-subsystem.h"
#include "wdt.h"
#include "system-led.h"
#include "ntc.h"
#include "adc.h"
#include "usart_tx.h"
#include "systick.h"
#include "wbmcu_system.h"
#include "array_size.h"
#include "rtc-alarm-subsystem.h"

#define WBEC_STARTUP_TIMEOUT_MS                     20

static const char fwver_chars[] = { MODBUS_DEVICE_FW_VERSION_STRING };
static const gpio_pin_t gpio_linux_power = { EC_GPIO_LINUX_POWER };

// Причина включения питания Linux
enum poweron_reason {
    REASON_POWER_ON,        // Подано питание на WB
    REASON_POWER_KEY,       // Нажата кнопка питания (до этого было выключено)
    REASON_RTC_ALARM,       // Будильник
    REASON_REBOOT,          // Перезагрузка
    REASON_REBOOT_NO_ALARM, // Перезагрузка вместо выключения, т.к. нет будильника
    REASON_WATHDOG,         // Сработал watchdog
    REASON_UNKNOWN,         // Неизветсно что (на всякий случай)
};

// Запрос из Linux на управление питанием
enum linux_powerctrl_req {
    LINUX_POWERCTRL_NO_ACTION,
    LINUX_POWERCTRL_OFF,
    LINUX_POWERCTRL_REBOOT,
};

static const char * power_reason_strings[] = {
    "Wiren Board supply on",
    "Power key pressed",
    "RTC alarm",
    "Reboot",
    "Reboot instead of poweroff",
    "Watchdog",
    "Unknown",
};

// Состояние алгоритма EC
enum wbec_state {
    WBEC_STATE_WAIT_STARTUP,
    WBEC_STATE_VOLTAGE_CHECK,
    WBEC_STATE_WORKING,
    WBEC_STATE_WAIT_LINUX_POWER_OFF,
    WBEC_STATE_WAIT_POWER_RESET,
};

struct wbec_ctx {
    enum wbec_state state;
    systime_t timestamp;
};

static struct REGMAP_INFO wbec_info = {
    .wbec_id = WBEC_ID,
    MODBUS_DEVICE_FW_VERSION_NUMBERS,
};

static struct wbec_ctx wbec_ctx;

static inline void linux_power_off(void)
{
    GPIO_S_RESET(gpio_linux_power);
}

static inline void linux_power_on(void)
{
    GPIO_S_SET(gpio_linux_power);
}

static inline void linux_power_gpio_init(void)
{
    GPIO_S_SET_PUSHPULL(gpio_linux_power);
    GPIO_S_SET_OUTPUT(gpio_linux_power);
}

static inline void goto_standby(void)
{
    // Apply pull-up and pull-down configuration
    PWR->CR3 |= PWR_CR3_APC;

    // Clear WKUP flags
    PWR->SCR = PWR_SCR_CWUF;

    // SLEEPDEEP
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;

    // 011: Standby mode
    PWR->CR1 |= PWR_CR1_LPMS_0 | PWR_CR1_LPMS_1;

    __WFI();
    while (1) {};
}

static inline enum poweron_reason get_poweron_reason(void)
{
    enum poweron_reason reason;
    if (PWR->SR1 & PWR_SR1_SBF) {
        PWR->SCR = PWR_SCR_CSBF;
        if (PWR->SR1 & PWR_SR1_WUF1) {
            PWR->SCR = PWR_SCR_CWUF1;
            reason = REASON_POWER_KEY;
        } else if (PWR_SR1_WUFI) {
            PWR->SCR = PWR_SR1_WUFI;
            reason = REASON_RTC_ALARM;
        } else {
            reason = REASON_UNKNOWN;
        }
    } else {
        reason = REASON_POWER_ON;
    }
    return reason;
}

static inline const char * get_poweron_reason_string(enum poweron_reason r)
{
    if (r >= ARRAY_SIZE(power_reason_strings)) {
        return "Unknown";
    }
    return power_reason_strings[r];
}

static inline void collect_adc_data(struct REGMAP_ADC_DATA * adc)
{
    // Get voltages
    adc->v_a1 = adc_get_ch_mv(ADC_CHANNEL_ADC_IN1);
    adc->v_a2 = adc_get_ch_mv(ADC_CHANNEL_ADC_IN2);
    adc->v_a3 = adc_get_ch_mv(ADC_CHANNEL_ADC_IN3);
    adc->v_a4 = adc_get_ch_mv(ADC_CHANNEL_ADC_IN4);
    adc->v_in = adc_get_ch_mv(ADC_CHANNEL_ADC_V_IN);
    adc->v_5_0 = adc_get_ch_mv(ADC_CHANNEL_ADC_5V);
    adc->v_3_3 = adc_get_ch_mv(ADC_CHANNEL_ADC_3V3);
    adc->vbus_console = adc_get_ch_mv(ADC_CHANNEL_ADC_VBUS_DEBUG);
    adc->vbus_network = adc_get_ch_mv(ADC_CHANNEL_ADC_VBUS_NETWORK);

    // Calc NTC temp
    fix16_t ntc_raw = adc_get_ch_mv(ADC_CHANNEL_ADC_NTC);
    // Convert to x10 *C
    adc->temp = fix16_to_int(
        fix16_mul(
            ntc_get_temp(ntc_raw),
            F16(100)
        )
    );
}

static inline enum linux_powerctrl_req get_linux_powerctrl_req(void)
{
    // Linux is ready to power off
    if (regmap_is_region_changed(REGMAP_REGION_POWER_CTRL)) {
        struct REGMAP_POWER_CTRL p;
        regmap_get_region_data(REGMAP_REGION_POWER_CTRL, &p, sizeof(p));
        if (p.off) {
            return LINUX_POWERCTRL_OFF;
        }
        if (p.reboot) {
            return LINUX_POWERCTRL_REBOOT;
        }

        regmap_clear_changed(REGMAP_REGION_POWER_CTRL);
    }

    return LINUX_POWERCTRL_NO_ACTION;
}

void wbec_init(void)
{
    // Здесь нельзя инициализировать GPIO управления питанием
    // т.к. исходно оно можут быть включено (если это была перепрошивка EC)
    // или выключено (в остальных случаях)
    // И нужно сначала понять, какой случай, а потом настраивать GPIO как выход
    // Иначе управление питанием перехватится раньше, чем нужно
    // Инициализация GPIO выполняется в WBEC_STATE_VOLTAGE_CHECK

    // Подтяжка вниз в режиме standby для GPIO управления питанием линукса
    // Таким образом, в standby линукс будет выключен
    PWR->PDCRD |= (1 << gpio_linux_power.pin);

    // Подтяжка вверх для кнопки питания в standby (кнопка замыкает вход на землю)
    PWR->PUCRA |= (1 << PWR_KEY_PIN);

    // Enable internal wakeup line (for RTC)
    PWR->CR3 |= PWR_CR3_EIWUL;

    // Set BUTTON pin as wakeup source
    PWR->CR3 |= PWR_CR3_EWUP1;
    // Set falling edge as wakeup trigger
    PWR->CR4 |= PWR_CR4_WP1;

    wbec_info.poweron_reason = get_poweron_reason();

    regmap_set_region_data(REGMAP_REGION_INFO, &wbec_info, sizeof(wbec_info));

    wdt_set_timeout(WDEC_WATCHDOG_INITIAL_TIMEOUT_S);

    wbec_ctx.state = WBEC_STATE_WAIT_STARTUP;
    wbec_ctx.timestamp = 0;
}

void wbec_do_periodic_work(void)
{
    struct REGMAP_ADC_DATA adc;
    collect_adc_data(&adc);
    regmap_set_region_data(REGMAP_REGION_ADC_DATA, &adc, sizeof(adc));

    enum linux_powerctrl_req linux_powerctrl_req = get_linux_powerctrl_req();


    switch (wbec_ctx.state) {
    case WBEC_STATE_WAIT_STARTUP:
        // После загрузки МК нужно подождать некоторое время, пока пройдут переходные процессы
        // и измерятся значения АЦП
        // В это время линукс выключен, т.к. на плате есть RC-цепочка, которая держит питание выключенным
        // примерно хххх мс после включения питания
        // При пробужении из спящего режима RC-цепочка так же работает и держит линукс выключенным
        // после пробуждения МК
        if (systick_get_time_since_timestamp(wbec_ctx.timestamp) > WBEC_STARTUP_TIMEOUT_MS) {
            wbec_ctx.state = WBEC_STATE_VOLTAGE_CHECK;
        }
        break;

    case WBEC_STATE_VOLTAGE_CHECK:
        // В этом состоянии линукс всё ещё выключен
        // Тут нужно проверить напряжения и температуру (в будущем)
        // Также возможна ситуация, когда при обновлении прошивки МК перезагружается,
        // а линукс в это время работает. Тогда его не надо перезагружать.
        // Факт работы линукса определяется по наличию +5В
        if ((wbec_info.poweron_reason == REASON_POWER_ON) && (adc.v_5_0 > 4500)) {
            // Если после включени МК +5В есть - значить линукс уже работает
            // Не нужно выводить информацию в уарт
            // Тут ничего не нужно делать
        } else {
            // В ином случае перехватываем питание (выключаем)
            // И отправляем информацию в уарт
            linux_power_off();
            linux_power_gpio_init();

            // Блокирующая отправка здесь - не страшно,
            // т.к. устройство в этом состоянии больше ничего не делает
            usart_tx_strn_blocking("Wirenboard Embedded Controller\n");
            usart_tx_strn_blocking("Version: ");
            usart_tx_str_blocking(fwver_chars, ARRAY_SIZE(fwver_chars));
            usart_tx_strn_blocking("\n");
            usart_tx_strn_blocking("Git info: ");
            usart_tx_strn_blocking(MODBUS_DEVICE_GIT_INFO);
            usart_tx_strn_blocking("\n\n");

            usart_tx_strn_blocking("Power on reason: ");
            usart_tx_strn_blocking(get_poweron_reason_string(wbec_info.poweron_reason));
            usart_tx_strn_blocking("\n\n");

            // TODO Display voltages and temp

            usart_tx_strn_blocking("Power on now...\n\n");
        }

        // Перед включеним питания сбросим события с кнопки
        pwrkey_handle_short_press();
        pwrkey_handle_long_press();
        // Установим время WDT и запустим его
        wdt_set_timeout(WDEC_WATCHDOG_INITIAL_TIMEOUT_S);
        // TODO: uncomment
        // wdt_start_reset();

        linux_power_on();
        linux_power_gpio_init();
        wbec_ctx.state = WBEC_STATE_WORKING;
        break;

    case WBEC_STATE_WORKING:
        // В этом состоянии линукс работает
        // И всё остальное тоже работает

        // Если было короткое нажатие - отправляем в линукс запрос на выключение
        // И переходим в состояние ожидания выключения
        if (pwrkey_handle_short_press()) {
            wdt_stop();
            irq_set_flag(IRQ_PWR_OFF_REQ);
            system_led_blink(250, 250);
            wbec_ctx.state = WBEC_STATE_WAIT_LINUX_POWER_OFF;
            wbec_ctx.timestamp = systick_get_system_time();
        }

        // Если было долгое нажатие - выключаемся сразу
        if (pwrkey_handle_long_press()) {
            linux_power_off();
            usart_tx_strn_blocking("\nPower key long press detected, power off without delay.\n\n");
            goto_standby();
        }

        if (linux_powerctrl_req == LINUX_POWERCTRL_OFF) {
            // Если прилетел запрос из линукса на выключение
            // Это была выполнена команда `poweroff` или `rtcwake -moff`
            // Не должно быть возможности программно выключить контроллер так, чтобы
            // нужно было ехать нажить кнопку чтобы его включить обратно
            // Поэтому здесь нужно проверить наличие будильника и если он есть - выключиться
            // иначе - перезагрузиться
            linux_power_off();
            usart_tx_strn_blocking("\nPower off request from Linux.\n\n");
            if (rtc_alarm_is_alarm_enabled()) {
                usart_tx_strn_blocking("\nAlarm is set, power is off.\n\n");
                goto_standby();
            } else {
                usart_tx_strn_blocking("\nAlarm not set, reboot system instead of power off.\n\n");
                wbec_info.poweron_reason = REASON_REBOOT_NO_ALARM;
                wbec_ctx.state = WBEC_STATE_WAIT_POWER_RESET;
                wbec_ctx.timestamp = systick_get_system_time();
            }
        } else if (linux_powerctrl_req == LINUX_POWERCTRL_REBOOT) {
            // Если запрос на перезагрузку - перезагружается
            wbec_info.poweron_reason = REASON_REBOOT;
            wbec_ctx.state = WBEC_STATE_WAIT_POWER_RESET;
            wbec_ctx.timestamp = systick_get_system_time();
            linux_power_off();
            usart_tx_strn_blocking("\nReboot request, reset power.\n\n");
        }

        // Если сработал WDT - перезагружаемся по питанию
        if (wdt_handle_timed_out()) {
            system_led_blink(50, 50);
            wbec_info.poweron_reason = REASON_WATHDOG;
            wbec_ctx.state = WBEC_STATE_WAIT_POWER_RESET;
            wbec_ctx.timestamp = systick_get_system_time();
            linux_power_off();
            usart_tx_strn_blocking("\nWatchdog is timed out, reset power.\n\n");
        }

        break;

    case WBEC_STATE_WAIT_LINUX_POWER_OFF:
        // В это состояние попадаем после короткого нажатия кнопки
        // Запрос на выключение в линукс уже отправлен (бит в регистре установлен)
        // Тут нужно подождать и если линукс не отправит запрос на
        // выключение питания - то выключить его принудительно

        if (linux_powerctrl_req == LINUX_POWERCTRL_OFF) {
            // Штатное выключение (запрос на выключение был с кнопки)
            linux_power_off();
            usart_tx_strn_blocking("\nPower off request from Linux after power key pressed. Power is off.\n\n");
            goto_standby();
        } else if (systick_get_time_since_timestamp(wbec_ctx.timestamp) >= WBEC_LINUX_POWER_OFF_DELAY_MS) {
            // Аварийное выключение (можно как-то дополнительно обработать)
            linux_power_off();
            usart_tx_strn_blocking("\nNo power off request from Linux after power key pressed. Power is forced off.\n\n");
            goto_standby();
        }

        break;

    case WBEC_STATE_WAIT_POWER_RESET:
        // В это состояние попадаем, когда нужно дернуть питание для перезагрузки
        // Питание линукса в этом состоянии выключено
        // Тут нужно подождать, пока разрядятся конденсаторы и перейти в INIT
        // Где всё заново включится

        if (systick_get_time_since_timestamp(wbec_ctx.timestamp) >= WBEC_POWER_RESET_TIME_MS) {
            wbec_ctx.state = WBEC_STATE_VOLTAGE_CHECK;
        }

        break;
    }
}

