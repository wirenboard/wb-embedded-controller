#include "config.h"
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
#include "rtc.h"
#include "voltage-monitor.h"
#include "linux-power-control.h"
#include "mcu-pwr.h"
#include "rcc.h"
#include "console.h"

#define LINUX_POWERON_REASON(m) \
    m(REASON_POWER_ON,        "Power supply on"        ) \
    m(REASON_POWER_KEY,       "Power button"                 ) \
    m(REASON_RTC_ALARM,       "RTC alarm"                    ) \
    m(REASON_REBOOT,          "Reboot"                       ) \
    m(REASON_REBOOT_NO_ALARM, "Reboot instead of poweroff"   ) \
    m(REASON_WATCHDOG,        "Watchdog"                     ) \
    m(REASON_PMIC_OFF,        "PMIC is unexpectedly off"     ) \
    m(REASON_UNKNOWN,         "Unknown"                      ) \

#define __LINUX_POWERON_REASON_NAME(name, string)           name,
#define __LINUX_POWERON_REASON_STRING(name, string)         string,

static const char fwver_chars[] = { FW_VERSION_STRING };

// Причина включения питания Linux
enum linux_poweron_reason {
    LINUX_POWERON_REASON(__LINUX_POWERON_REASON_NAME)
};
static const char * linux_power_reason_strings[] = {
    LINUX_POWERON_REASON(__LINUX_POWERON_REASON_STRING)
};

// Запрос из Linux на управление питанием
enum linux_powerctrl_req {
    LINUX_POWERCTRL_NO_ACTION,
    LINUX_POWERCTRL_OFF,
    LINUX_POWERCTRL_REBOOT,
    LINUX_POWERCTRL_PMIC_RESET,
};

// Состояние алгоритма EC
enum wbec_state {
    WBEC_STATE_WAIT_STARTUP,
    WBEC_STATE_VOLTAGE_CHECK,
    WBEC_STATE_TEMP_CHECK_LOOP,
    WBEC_STATE_POWER_ON_SEQUENCE_WAIT,
    WBEC_STATE_WORKING,
    WBEC_STATE_POWER_OFF_SEQUENCE_WAIT,
};

struct wbec_ctx {
    enum wbec_state state;
    systime_t timestamp;
    systime_t pwrkey_pressed_timestamp;
    bool pwrkey_pressed;
    bool linux_booted;
    bool linux_initial_powered_on;
    unsigned power_loss_cnt;
    systime_t power_loss_timestamp;
};

static struct REGMAP_INFO wbec_info = {
    .wbec_id = WBEC_ID,
    .hwrev = 0,
    .fwrev = { FW_VERSION_NUMBERS },
    .poweron_reason = REASON_UNKNOWN,
};

static struct wbec_ctx wbec_ctx;

static void new_state(enum wbec_state s)
{
    wbec_ctx.state = s;
    wbec_ctx.timestamp = systick_get_system_time_ms();

    switch (wbec_ctx.state) {
    case WBEC_STATE_WAIT_STARTUP:               system_led_blink(5,   100);     break;
    case WBEC_STATE_VOLTAGE_CHECK:              system_led_blink(5,   100);     break;
    case WBEC_STATE_TEMP_CHECK_LOOP:            system_led_blink(5,   100);     break;
    case WBEC_STATE_POWER_ON_SEQUENCE_WAIT:     system_led_blink(50,  50);      break;
    case WBEC_STATE_WORKING:                    system_led_blink(500, 1000);    break;
    case WBEC_STATE_POWER_OFF_SEQUENCE_WAIT:    system_led_blink(50,  50);      break;
    default:                                    system_led_enable();            break;
    }
}

static inline systime_t in_state_time_ms(void)
{
    return systick_get_time_since_timestamp(wbec_ctx.timestamp);
}

static inline const char * get_poweron_reason_string(enum linux_poweron_reason r)
{
    if (r >= ARRAY_SIZE(linux_power_reason_strings)) {
        return "Unknown";
    }
    return linux_power_reason_strings[r];
}

static inline void collect_adc_data(struct REGMAP_ADC_DATA * adc)
{
    // Get voltages
    adc->v_a1 = adc_get_ch_mv(ADC_CHANNEL_ADC_IN1);
    adc->v_a2 = adc_get_ch_mv(ADC_CHANNEL_ADC_IN2);
    adc->v_a3 = adc_get_ch_mv(ADC_CHANNEL_ADC_IN3);
    adc->v_a4 = adc_get_ch_mv(ADC_CHANNEL_ADC_IN4);
    adc->v_5_0 = adc_get_ch_mv(ADC_CHANNEL_ADC_5V);
    adc->v_3_3 = adc_get_ch_mv(ADC_CHANNEL_ADC_3V3);
    adc->vbus_console = adc_get_ch_mv(ADC_CHANNEL_ADC_VBUS_DEBUG);
    adc->vbus_network = adc_get_ch_mv(ADC_CHANNEL_ADC_VBUS_NETWORK);

    if (vmon_get_ch_status(VMON_CHANNEL_V_IN)) {
        adc->v_in = adc_get_ch_mv(ADC_CHANNEL_ADC_V_IN);
    } else {
        adc->v_in = 0;
    }

    // Calc NTC temp
    fix16_t ntc_raw = adc_get_ch_adc_raw(ADC_CHANNEL_ADC_NTC);
    // Convert to x100 *C
    adc->temp = fix16_to_int(
        fix16_mul(
            ntc_convert_adc_raw_to_temp(ntc_raw),
            F16(100)
        )
    );
}

static inline enum linux_powerctrl_req get_linux_powerctrl_req(void)
{
    enum linux_powerctrl_req ret = LINUX_POWERCTRL_NO_ACTION;

    if (regmap_is_region_changed(REGMAP_REGION_POWER_CTRL)) {
        struct REGMAP_POWER_CTRL p;
        regmap_get_region_data(REGMAP_REGION_POWER_CTRL, &p, sizeof(p));
        // Linux is ready to power off
        if (p.off) {
            p.off = 0;
            ret = LINUX_POWERCTRL_OFF;
        } else if (p.reboot) {
            p.reboot = 0;
            ret = LINUX_POWERCTRL_REBOOT;
        } else if (p.reset_pmic) {
            p.reset_pmic = 0;
            ret = LINUX_POWERCTRL_PMIC_RESET;
        }

        regmap_clear_changed(REGMAP_REGION_POWER_CTRL);
        regmap_set_region_data(REGMAP_REGION_POWER_CTRL, &p, sizeof(p));
    }

    return ret;
}

void wbec_init(void)
{
    // Здесь нельзя инициализировать GPIO управления питанием
    // т.к. исходно оно можут быть включено (если это была перепрошивка EC)
    // или выключено (в остальных случаях)
    // И нужно сначала понять, какой случай, а потом настраивать GPIO как выход
    // Иначе управление питанием перехватится раньше, чем нужно
    // Инициализация GPIO выполняется в WBEC_STATE_VOLTAGE_CHECK

    // Enable internal wakeup line (for RTC)
    PWR->CR3 |= PWR_CR3_EIWUL;

    enum mcu_poweron_reason mcu_poweron_reason = mcu_get_poweron_reason();

    // Независимо от причины включения нужно измерить напряжение на линии 5В
    // Работаем на частоте 1 МГц для снижения потребления
    while (!adc_get_ready()) {};
    bool vcc_5v_ok = vmon_check_ch_once(VMON_CHANNEL_V50);
    enum mcu_vcc_5v_state vcc_5v_last_state = mcu_get_vcc_5v_last_state();

    /**
     * Тут возможны варианты:
     * 1) включились от POWER ON - первый раз появилось питание на ЕС или перепрошились
     *    - нужно включаться в обычном режиме
     * 2) включились от кнопки
     *    - нужно проверить на антидребезг и включиться в обычном режиме
     * 3) включились от будильника
     *    - нужно включиться в обычном режиме
     * 4) включились от периодического пробуждения RTC
     *    - нужно ждать появления питания на линии +5В - измерять её относительно INT VREF
     *    - если питание появилось - включиться в обычном режиме
     */

    switch (mcu_poweron_reason) {
    case MCU_POWERON_REASON_UNKNOWN:
    case MCU_POWERON_REASON_POWER_ON:
    default:
        // Если включились от POWER ON - первый раз появилось питание на ЕС или перепрошились
        // Нужно включаться в обычном режиме
        // (просто идём дальше)
        wbec_info.poweron_reason = REASON_POWER_ON;
        break;

    case MCU_POWERON_REASON_POWER_KEY:
        // Если включились от кнопки
        // Нужно проверить на антидребезг и включиться в обычном режиме
        // (просто идём дальше)
        pwrkey_init();
        while (!pwrkey_ready()) {
            pwrkey_do_periodic_work();
        }
        if (pwrkey_pressed()) {
            wbec_info.poweron_reason = REASON_POWER_KEY;
        } else {
            // Если кнопка не нажата (или нажата коротко и не прошла антидребезг) - засыпаем
            linux_cpu_pwr_seq_off_and_goto_standby(WBEC_PERIODIC_WAKEUP_NEXT_TIMEOUT_S);
        }
        break;

    case MCU_POWERON_REASON_RTC_ALARM:
        // Если включились от будильника
        // Нужно включиться в обычном режиме
        // (просто идём дальше)
        wbec_info.poweron_reason = REASON_RTC_ALARM;
        break;

    case MCU_POWERON_REASON_RTC_PERIODIC_WAKEUP:
        // Если включилить от периодического пробуждения RTC, значит:
        // 1) либо у ЕС есть питание от WBMZ
        // 2) либо выключились по будильнику при наличии Vin
        // Логика тут такая: если +5В **появились** - включаемся.
        // Для этого надо помнить предыдущее состояние +5В - храним его в RTC домене
        // Напряжение питания ЕС может плавать от 2.7 до 3.3 В
        // Тут нужно ждать появления питания на линии +5В - измерять её относительно INT VREF

        if (vcc_5v_ok) {
            // Возможна ситуация, если долго держать EC RESET, то причина
            // POWER ON переопределяется причиной PERIODIC WAKE UP
            // В этом случае тоже нужно включиться.
            // Понять это можно, измерив 3.3В
            bool vcc_3v3_ok = vmon_check_ch_once(VMON_CHANNEL_V33);

            if ((vcc_3v3_ok) || (vcc_5v_last_state == MCU_VCC_5V_STATE_OFF)) {
                // Питание появилось или есть 3.3В (уже работаем) - включаемся в обычном режиме
                // (просто идём дальше)
                wbec_info.poweron_reason = REASON_POWER_ON;
            } else {
                linux_cpu_pwr_seq_off_and_goto_standby(WBEC_PERIODIC_WAKEUP_NEXT_TIMEOUT_S);
            }
        } else {
            if (vcc_5v_last_state == MCU_VCC_5V_STATE_ON) {
                // Питание было и пропало - сохраняем последнее состояние в RTC домене
                mcu_save_vcc_5v_last_state(MCU_VCC_5V_STATE_OFF);
            }
            linux_cpu_pwr_seq_off_and_goto_standby(WBEC_PERIODIC_WAKEUP_NEXT_TIMEOUT_S);
        }
        break;
    }

    // Проверяем, что питание есть
    if (vcc_5v_ok) {
        // Питание есть - включаемся в обычном режиме
        // (просто идём дальше)
        // WBMZ включится отдельным алторитмом, если Vin будет более 11.5 В
    } else {
        // Питания нет - включаем WBMZ
        linux_cpu_pwr_seq_enable_wbmz();
    }

    // Если дошли до этого места, надо включиться в обычном режиме
    pwrkey_set_debounce_ms(PWRKEY_DEBOUNCE_MS_OFF);
    rtc_disable_periodic_wakeup();
    new_state(WBEC_STATE_WAIT_STARTUP);
}

void wbec_do_periodic_work(void)
{
    struct REGMAP_ADC_DATA adc;
    collect_adc_data(&adc);
    regmap_set_region_data(REGMAP_REGION_ADC_DATA, &adc, sizeof(adc));
    regmap_set_region_data(REGMAP_REGION_INFO, &wbec_info, sizeof(wbec_info));

    enum linux_powerctrl_req linux_powerctrl_req = get_linux_powerctrl_req();

    switch (wbec_ctx.state) {
    case WBEC_STATE_WAIT_STARTUP:
        // После загрузки МК нужно подождать готовности vmon, пока пройдут переходные процессы
        // и измерятся значения АЦП
        // В это время линукс выключен, т.к. на плате есть RC-цепочка, которая держит питание выключенным
        // примерно хххх мс после включения питания
        // При пробужении из спящего режима RC-цепочка так же работает и держит линукс выключенным
        // после пробуждения МК
        // Также возможна ситуация, когда при обновлении прошивки МК перезагружается,
        // а линукс в это время работает. Тогда его не надо перезагружать.
        // Факт работы линукса определяется по наличию +3.3В
        if (vmon_ready()) {
            if ((wbec_info.poweron_reason == REASON_POWER_ON) && (vmon_get_ch_status(VMON_CHANNEL_V33))) {
                // Если после включения МК +3.3В есть - значит линукс уже работает
                // Не нужно выводить информацию в уарт
                // Тут ничего не нужно делать
                // Инициализируем GPIO управления питанием сразу во включенном состоянии
                linux_cpu_pwr_seq_init(1);
                wbec_ctx.linux_initial_powered_on = true;
                new_state(WBEC_STATE_POWER_ON_SEQUENCE_WAIT);
            } else {
                // В ином случае (3.3В нет - линукс выключен) перехватываем питание (выключаем)
                // На момент включения питание держится выключенным RC-цепочкой на ключе питания 5В.
                // Нужно перехватить питание (удержвать его выключенным сигналом с ЕС)
                // И отправить информацию в уарт
                linux_cpu_pwr_seq_init(0);

                console_print("\r\n\r\n"); // Это может быть первым сообщением сессии, отделим его от предыдущего вывода
                console_print_w_prefix("Starting up...\r\n");
                new_state(WBEC_STATE_VOLTAGE_CHECK);
            }
            // Заполним стуктуру INFO данными
            // hwrev определяется делителем на плате между AVCC и GND. Возможные значения: [0, 4095]
            // EC на данный момент это никак не использует, в линуксе можно получить через sysfs
            // cat /sys/bus/spi/devices/spi0.0/hwrev
            wbec_info.hwrev = fix16_to_int(adc_get_ch_adc_raw(ADC_CHANNEL_ADC_HW_VER));
            regmap_set_region_data(REGMAP_REGION_INFO, &wbec_info, sizeof(wbec_info));
            // Сбросим счётчик потерь питания
            wbec_ctx.power_loss_cnt = 0;
            wbec_ctx.power_loss_timestamp = systick_get_system_time_ms();
        }
        break;

    case WBEC_STATE_VOLTAGE_CHECK:
        // Сюда попадаем только один раз при включении
        // В дальнейшем при перезагрузках сюда уже не попадаем
        // В этом состоянии линукс всё ещё выключен
        // Тут нужно проверить напряжения и температуру (в будущем)

        // Блокирующая отправка здесь - не страшно,
        // т.к. устройство в этом состоянии больше ничего не делает

        // Если включились по USB (в общем случае - не по Vin) - нужно
        // подождать несколько секунд, чтобы не пропадали первые дебаг-сообщения
        if (wbec_info.poweron_reason == REASON_POWER_ON) {
            if (vmon_get_ch_status(VMON_CHANNEL_VBUS_DEBUG)) {
                if (in_state_time_ms() < WBEC_LINUX_POWER_ON_DELAY_FROM_USB) {
                    static unsigned counter = 0;
                    if ((counter++) % 17 == 0) {
                        console_print("\e[?25l"); // hide cursor
                        console_print("\r");
                        console_print_w_prefix("Pausing for 5 seconds to allow PC to detect USB console ");
                        console_print_spinner(counter++);
                        // fill with spaces to clear possible leftover noise
                        for (unsigned int i = 0; i < 30; ++i) {
                            console_print(" ");
                        }
                    }
                    break;
                }
            }
        }

        console_print("\e[?25h"); // show cursor
        console_print("\r\n\n");
        console_print_w_prefix("Wiren Board Embedded Controller\r\n");
        console_print_w_prefix("Firmware version: ");
        usart_tx_buf_blocking(fwver_chars, ARRAY_SIZE(fwver_chars));
        console_print("\r\n");
        console_print_w_prefix("Git info: ");
        console_print(MODBUS_DEVICE_GIT_INFO);
        console_print("\r\n");

        console_print_w_prefix("Power on reason: ");
        console_print(get_poweron_reason_string(wbec_info.poweron_reason));
        console_print("\r\n");

        console_print_w_prefix("RTC time: ");
        console_print_time_now();
        console_print("\r\n");

        console_print_w_prefix("Board temperature: ");
        console_print_fixed_point(adc.temp / 10, 1);
        console_print("ºC\r\n");

        console_print_w_prefix("Vin: ");
        console_print_fixed_point(adc.v_in / 100, 1);
        console_print("V, USB Net: ");
        console_print_fixed_point(adc.vbus_network / 100, 1);
        console_print("V, USB Console: ");
        console_print_fixed_point(adc.vbus_console / 100, 1);
        console_print("V\r\n");

        if (adc.temp < WBEC_MINIMUM_WORKING_TEMPERATURE_C_X100) {
            console_print_w_prefix("WARNING: Board temperature is too low!\r\n");
            new_state(WBEC_STATE_TEMP_CHECK_LOOP);
        } else {
            console_print_w_prefix("Turning on the main CPU; all future debug messages will originate from the CPU.\r\n\n\n");
            // После отправки данных включаем линукс
            linux_cpu_pwr_seq_on();
            new_state(WBEC_STATE_POWER_ON_SEQUENCE_WAIT);
        }

        break;

    case WBEC_STATE_TEMP_CHECK_LOOP:
        // В этом состоянии линукс выключен, проверяем температуру
        // Сидим тут до тех пор, пока температура не станет выше -40
        if (in_state_time_ms() > 5000) {
            if (adc.temp < WBEC_MINIMUM_WORKING_TEMPERATURE_C_X100) {
                console_print_w_prefix("Board temperature is below -40°C! Rechecking in 5 seconds\r\n");
                new_state(WBEC_STATE_TEMP_CHECK_LOOP);
            } else {
                console_print_w_prefix("Temperature is OK!\r\n");
                console_print_w_prefix("Turning on the main CPU; all future debug messages will originate from the CPU\r\n\n\n");
                linux_cpu_pwr_seq_on();
                new_state(WBEC_STATE_POWER_ON_SEQUENCE_WAIT);
            }
        }
        break;

    case WBEC_STATE_POWER_ON_SEQUENCE_WAIT:
        // В этом состоянии ждём
        // когда отработает логика включения питания начатая во время вызова linux_cpu_pwr_seq_on()
        if (!linux_cpu_pwr_seq_is_busy()) {
            // Нужно проигнорировать все нажатия до включения питания линукса
            pwrkey_handle_short_press();
            pwrkey_handle_long_press();
            // Установим время WDT и запустим его
            wdt_set_timeout(WBEC_WATCHDOG_INITIAL_TIMEOUT_S);
            wdt_start_reset();
            wdt_handle_timed_out();
            // Флаг linux_initial_powered_on нужен чтобы понять, что линукс уже работал на момент включения ЕС
            // В этом случае считаем его уже загруженным
            wbec_ctx.linux_booted = wbec_ctx.linux_initial_powered_on;
            wbec_ctx.linux_initial_powered_on = false;
            wbec_ctx.pwrkey_pressed = false;
            // Как только питание включилось - переходим в рабочий режим
            new_state(WBEC_STATE_WORKING);
        }
        break;

    case WBEC_STATE_WORKING:
        // В этом состоянии линукс работает
        // И всё остальное тоже работает

        // Ждём загрузки линукс (просто по времени)
        if ((!wbec_ctx.linux_booted) && (in_state_time_ms() > WBEC_LINUX_BOOT_TIME_MS)) {
            wbec_ctx.linux_booted = true;
        }

        if (wbec_ctx.linux_booted) {
            // Если линукс загружен - отправляем запрос на выключение
            // При этом не ждём полноценного нажатия, а отправляем запрос сразу
            // Если выполняется долгое нажатие, то есть шанс что линукс успеет
            // корректно выключиться
            if (pwrkey_pressed()) {
                wbec_ctx.pwrkey_pressed = true;
                wbec_ctx.pwrkey_pressed_timestamp = systick_get_system_time_ms();
                irq_set_flag(IRQ_PWR_OFF_REQ);
            }
        } else {
            // Если линукс не загружен - выключаемся по питанию сразу же
            // При это ждём полноценное нажатие, т.к. есть вероятность, что
            // пока держат кнопку - линукс загрузится и успеет штатно выключиться
            if (pwrkey_handle_short_press()) {
                linux_cpu_pwr_seq_hard_off();
                new_state(WBEC_STATE_POWER_OFF_SEQUENCE_WAIT);
            }
        }

        // Если флаг нажатой кнопки висит слишком долго (линукс по каким-то причинам не отреагировал)
        // Нужно его сбросить, т.к. он влияет на решение для выключения по poweroff
        if (wbec_ctx.pwrkey_pressed) {
            if (systick_get_time_since_timestamp(wbec_ctx.pwrkey_pressed_timestamp) > WBEC_LINUX_POWER_OFF_DELAY_MS) {
                wbec_ctx.pwrkey_pressed = false;
            }
        }

        if (linux_powerctrl_req == LINUX_POWERCTRL_OFF) {
            // Если прилетел запрос из линукса на выключение
            // Это была выполнена команда `poweroff` или `rtcwake -m off`
            console_print("\r\n\n");
            console_print_w_prefix("Power off request from Linux.\r\n");
            bool wbmz = linux_cpu_pwr_seq_is_powered_from_wbmz();
            bool alarm = rtc_alarm_is_alarm_enabled();
            bool btn = wbec_ctx.pwrkey_pressed;

            if (alarm) {
                struct rtc_alarm rtc_alarm;
                rtc_get_alarm(&rtc_alarm);

                console_print_w_prefix("Time now is: ");
                console_print_time_now();
                console_print("\r\n");
                console_print_w_prefix("Alarm set to XXXX-XX-");
                console_print_dec_pad(BCD_TO_BIN(rtc_alarm.days), 2, '0');
                console_print(" ");
                console_print_dec_pad(BCD_TO_BIN(rtc_alarm.hours), 2, '0');
                console_print(":");
                console_print_dec_pad(BCD_TO_BIN(rtc_alarm.minutes), 2, '0');
                console_print(":");
                console_print_dec_pad(BCD_TO_BIN(rtc_alarm.seconds), 2, '0');
                console_print("\r\n");
            } else {
                console_print_w_prefix("Alarm: not set\r\n");
            }

            console_print_w_prefix("Power status: ");
            if (wbmz) {
                console_print("powered from WBMZ\r\n");
            } else {
                console_print("powered from external supply\r\n");
            }

            // Не должно быть возможности программно выключить контроллер так, чтобы
            // нужно было ехать нажимать кнопку чтобы его включить обратно
            // Поэтому здесь нужно проверить наличие будильника и если он есть - выключиться
            // иначе - перезагрузиться
            // Также разрешено выключаться по poweroff, если питаемся от WBMZ или если
            // выключились по кнопке
            if (wbmz || alarm || btn) {
                console_print_w_prefix("Powering off\r\n");
                linux_cpu_pwr_seq_hard_off();
                new_state(WBEC_STATE_POWER_OFF_SEQUENCE_WAIT);
            } else {
                console_print_w_prefix("Alarm not set, reboot system instead of power off.\r\n\n");
                wbec_info.poweron_reason = REASON_REBOOT_NO_ALARM;
                linux_cpu_pwr_seq_hard_reset();
                new_state(WBEC_STATE_POWER_ON_SEQUENCE_WAIT);
            }
        } else if (linux_powerctrl_req == LINUX_POWERCTRL_REBOOT) {
            // Если запрос на перезагрузку - перезагружается
            wbec_info.poweron_reason = REASON_REBOOT;
            console_print("\r\n\n");
            console_print_w_prefix("Reboot request, reset power.\r\n");
            linux_cpu_pwr_seq_hard_reset();
            new_state(WBEC_STATE_POWER_ON_SEQUENCE_WAIT);
        } else if (linux_powerctrl_req == LINUX_POWERCTRL_PMIC_RESET) {
            wbec_info.poweron_reason = REASON_REBOOT;
            console_print("\r\n\n");
            console_print_w_prefix("PMIC reset request, activate PMIC RESET line now\r\n\n");
            linux_cpu_pwr_seq_reset_pmic();
            new_state(WBEC_STATE_POWER_ON_SEQUENCE_WAIT);
        }

        // Если сработал WDT - перезагружаемся по питанию
        if (wdt_handle_timed_out()) {
            wbec_info.poweron_reason = REASON_WATCHDOG;
            console_print("\r\n\n");
            console_print_w_prefix("Watchdog is timed out, reset power.\r\n");
            linux_cpu_pwr_seq_hard_reset();
            new_state(WBEC_STATE_POWER_ON_SEQUENCE_WAIT);
        }

        // Если пропало 3.3В - пробуем перезапустить питание, но не более N раз за M минут
        // Если питание пропадает слишком часто - выключаемся
        // Это происходит, например, при питании через плохой USB кабель.
        // В результате PMIC выключается, но питание на линии 5В остаётся.
        // Ограничение по числу попыток нужно, чтобы избежать циклического перезапуска.
        if (!vmon_get_ch_status(VMON_CHANNEL_V33)) {
            if (!vmon_get_ch_status(VMON_CHANNEL_V50)) {
                // Если при этом нет напряжения на линии 5В - это означает, что выдернули питание
                // и не надо пытаться включиться заново
                linux_cpu_pwr_seq_hard_off();
                new_state(WBEC_STATE_POWER_OFF_SEQUENCE_WAIT);
            } else {
                if (systick_get_time_since_timestamp(wbec_ctx.power_loss_timestamp) < (WBEC_POWER_LOSS_TIMEOUT_MIN * 60 * 1000)) {
                    wbec_ctx.power_loss_cnt++;
                } else {
                    wbec_ctx.power_loss_cnt = 0;
                }
                wbec_ctx.power_loss_timestamp = systick_get_system_time_ms();
                if (wbec_ctx.power_loss_cnt > WBEC_POWER_LOSS_ATTEMPTS) {
                    console_print_w_prefix("Reaching power loss limit, power off and go to standby now\r\n");
                    // Чтобы включиться - нужно нажать кнопку или сбросить внешнее питание
                    linux_cpu_pwr_seq_hard_off();
                    new_state(WBEC_STATE_POWER_OFF_SEQUENCE_WAIT);
                } else {
                    console_print_w_prefix("3.3V is lost, try to reset power\r\n");
                    console_print_w_prefix("Enable WBMZ to prevent power loss under load\r\n");
                    wbec_info.poweron_reason = REASON_PMIC_OFF;
                    linux_cpu_pwr_seq_enable_wbmz();
                    linux_cpu_pwr_seq_hard_reset();
                    new_state(WBEC_STATE_POWER_ON_SEQUENCE_WAIT);
                }
            }
        }
        break;

    case WBEC_STATE_POWER_OFF_SEQUENCE_WAIT:
        // В этом состоянии ждем, когда закончится процесс выключения начатый в linux_cpu_pwr_seq_off()
        // Здесь ничего не делаем, выхода отсюда нет
        // МК в итоге перейдёт в standby

        break;
    }
}
