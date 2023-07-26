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

static const char fwver_chars[] = { MODBUS_DEVICE_FW_VERSION_STRING };

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
    WBEC_STATE_WAIT_POWER_ON,
    WBEC_STATE_WORKING,
    WBEC_STATE_WAIT_POWER_OFF,

    WBEC_STATE_WAIT_LINUX_POWER_OFF,
};

struct wbec_ctx {
    enum wbec_state state;
    systime_t timestamp;
    bool powered_from_wbmz;
    unsigned power_loss_cnt;
    systime_t power_loss_timestamp;
};

static struct REGMAP_INFO wbec_info = {
    .wbec_id = WBEC_ID,
    .hwrev = 0,
    .fwrev = { MODBUS_DEVICE_FW_VERSION_NUMBERS },
    .poweron_reason = REASON_UNKNOWN,
};

static struct wbec_ctx wbec_ctx;

static void new_state(enum wbec_state s)
{
    wbec_ctx.state = s;
    wbec_ctx.timestamp = systick_get_system_time_ms();

    switch (wbec_ctx.state) {
    case WBEC_STATE_WAIT_STARTUP:           system_led_blink(5,   100);     break;
    case WBEC_STATE_VOLTAGE_CHECK:          system_led_blink(5,   100);     break;
    case WBEC_STATE_TEMP_CHECK_LOOP:        system_led_blink(5,   100);     break;
    case WBEC_STATE_WAIT_POWER_ON:          system_led_blink(50,  50);      break;
    case WBEC_STATE_WORKING:                system_led_blink(500, 1000);    break;
    case WBEC_STATE_WAIT_POWER_OFF:         system_led_blink(50,  50);      break;
    case WBEC_STATE_WAIT_LINUX_POWER_OFF:   system_led_blink(250, 250);     break;
    default:                                system_led_enable();            break;
    }
}

static inline systime_t in_state_time(void)
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
            new_state(WBEC_STATE_WAIT_STARTUP);
        } else {
            // Если кнопка не нажата (или нажата коротко и не прошла антидребезг) - засыпаем
            mcu_goto_standby(WBEC_PERIODIC_WAKEUP_NEXT_TIMEOUT_S);
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

        if ((vcc_5v_ok) && (vcc_5v_last_state == MCU_VCC_5V_STATE_OFF)) {
            // Питание появилось - включаемся в обычном режиме
            // (просто идём дальше)
            wbec_info.poweron_reason = REASON_POWER_ON;
        } else if ((!vcc_5v_ok) && (vcc_5v_last_state == MCU_VCC_5V_STATE_ON)) {
            // Питание было и пропало - сохраняем послденее состояние в RTC домене
            mcu_save_vcc_5v_last_state(MCU_VCC_5V_STATE_OFF);
            mcu_goto_standby(WBEC_PERIODIC_WAKEUP_NEXT_TIMEOUT_S);
        } else {
            // Питание не появилось или не пропадало - засыпаем и ждём дальше
            // Здесь активен RTC periodic wakeup
            mcu_goto_standby(WBEC_PERIODIC_WAKEUP_NEXT_TIMEOUT_S);
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
        linux_pwr_enable_wbmz();
    }

    // Если дошли до этого места, надо включиться в обычном режиме
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
        // После загрузки МК нужно подождать некоторое время, пока пройдут переходные процессы
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
                // Если после включения МК +3.3В есть - значить линукс уже работает
                // Не нужно выводить информацию в уарт
                // Тут ничего не нужно делать
                // Инициализируем GPIO управления питанием сразу во включенном состоянии
                linux_pwr_init(1);
                new_state(WBEC_STATE_WAIT_POWER_ON);
            } else {
                // В ином случае перехватываем питание (выключаем)
                // И отправляем информацию в уарт
                linux_pwr_init(0);

                console_print("\r\n\r\n"); // Это может быть первым сообщением сессии, отделим его от предыдущего вывода
                console_print_w_prefix("Starting up...\r\n");
                new_state(WBEC_STATE_VOLTAGE_CHECK);
            }
            // Заполним стуктуру INFO данными
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
            if (!vmon_get_ch_status(VMON_CHANNEL_V_IN)) {
                if (in_state_time() < WBEC_LINUX_POWER_ON_DELAY_FROM_USB) {
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
        console_print("V\r\n");

        if (adc.temp < WBEC_MINIMUM_WORKING_TEMPERATURE_C_X100) {
            console_print_w_prefix("WARNING: Board temperature is too low!\r\n");
            new_state(WBEC_STATE_TEMP_CHECK_LOOP);
        } else {
            console_print_w_prefix("Turning on the main CPU; all future debug messages will originate from the CPU.\r\n\n\n");
            // После отправки данных включаем линукс
            linux_pwr_on();
            new_state(WBEC_STATE_WAIT_POWER_ON);
        }

        break;

    case WBEC_STATE_TEMP_CHECK_LOOP:
        // В этом состоянии линукс выключен, проверяем температуру
        // Сидим тут до тех пор, пока температура не станет выше -40
        if (in_state_time() > 5000) {
            if (adc.temp < WBEC_MINIMUM_WORKING_TEMPERATURE_C_X100) {
                console_print_w_prefix("Board temperature is below -40°C! Rechecking in 5 seconds\r\n");
                new_state(WBEC_STATE_TEMP_CHECK_LOOP);
            } else {
                console_print_w_prefix("Temperature is OK!\r\n");
                console_print_w_prefix("Turning on the main CPU; all future debug messages will originate from the CPU\r\n\n\n");
                linux_pwr_on();
                new_state(WBEC_STATE_WAIT_POWER_ON);
            }
        }
        break;

    case WBEC_STATE_WAIT_POWER_ON:
        // В этом состоянии ждём, пока логика включения питания его включит
        if (!linux_pwr_is_busy()) {
            // Сбросим события с кнопки
            pwrkey_handle_short_press();
            pwrkey_handle_long_press();
            // Установим время WDT и запустим его
            wdt_set_timeout(WBEC_WATCHDOG_INITIAL_TIMEOUT_S);
            wdt_start_reset();
            // Как только питание включилось - переходим в рабочий режим
            new_state(WBEC_STATE_WORKING);
        }
        break;

    case WBEC_STATE_WORKING:
        // В этом состоянии линукс работает
        // И всё остальное тоже работает

        // Если было короткое нажатие - отправляем в линукс запрос на выключение
        // И переходим в состояние ожидания выключения
        if (pwrkey_handle_short_press()) {
            wdt_stop();
            irq_set_flag(IRQ_PWR_OFF_REQ);
            new_state(WBEC_STATE_WAIT_LINUX_POWER_OFF);
        }

        if (linux_powerctrl_req == LINUX_POWERCTRL_OFF) {
            // Если прилетел запрос из линукса на выключение
            // Это была выполнена команда `poweroff` или `rtcwake -m off`
            // Не должно быть возможности программно выключить контроллер так, чтобы
            // нужно было ехать нажимать кнопку чтобы его включить обратно
            // Поэтому здесь нужно проверить наличие будильника и если он есть - выключиться
            // иначе - перезагрузиться
            console_print("\r\n\n");
            console_print_w_prefix("Power off request from Linux.\r\n");
            bool wbmz = linux_pwr_is_powered_from_wbmz();
            bool alarm = rtc_alarm_is_alarm_enabled();

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

            // Выключаться можно только если есть будильник ИЛИ питание от WBMZ
            if (wbmz || alarm) {
                // Если выключаемся при работающем WBMZ - дополнительно заводим RC periodic wakeup
                // чтобы просыпаться и следить за появлением входного напряжения
                if (wbmz) {
                    wbec_ctx.powered_from_wbmz = true;
                }
                console_print_w_prefix("Powering off\r\n");
                linux_pwr_off();
                new_state(WBEC_STATE_WAIT_POWER_OFF);
            } else {
                console_print_w_prefix("Alarm not set, reboot system instead of power off.\r\n\n");
                wbec_info.poweron_reason = REASON_REBOOT_NO_ALARM;
                linux_pwr_reset();
                new_state(WBEC_STATE_WAIT_POWER_ON);
            }
        } else if (linux_powerctrl_req == LINUX_POWERCTRL_REBOOT) {
            // Если запрос на перезагрузку - перезагружается
            wbec_info.poweron_reason = REASON_REBOOT;
            console_print("\r\n\n");
            console_print_w_prefix("Reboot request, reset power.\r\n");
            linux_pwr_reset();
            new_state(WBEC_STATE_WAIT_POWER_ON);
        } else if (linux_powerctrl_req == LINUX_POWERCTRL_PMIC_RESET) {
            wbec_info.poweron_reason = REASON_REBOOT;
            console_print("\r\n\n");
            console_print_w_prefix("PMIC reset request, activate PMIC RESET line now\r\n\n");
            linux_pwr_reset_pmic();
            new_state(WBEC_STATE_WAIT_POWER_ON);
        }

        // Если сработал WDT - перезагружаемся по питанию
        if (wdt_handle_timed_out()) {
            wbec_info.poweron_reason = REASON_WATCHDOG;
            console_print("\r\n\n");
            console_print_w_prefix("Watchdog is timed out, reset power.\r\n");
            linux_pwr_reset();
            new_state(WBEC_STATE_WAIT_POWER_ON);
        }

        // Если пропало 3.3В - пробуем перезапустить питание, но не более N раз за M минут
        // Если питание пропадает слишком часто - выключаемся
        if (!vmon_get_ch_status(VMON_CHANNEL_V33)) {
            if (systick_get_time_since_timestamp(wbec_ctx.power_loss_timestamp) < (WBEC_POWER_LOSS_TIMEOUT_MIN * 60 * 1000)) {
                wbec_ctx.power_loss_cnt++;
            } else {
                wbec_ctx.power_loss_cnt = 0;
            }
            wbec_ctx.power_loss_timestamp = systick_get_system_time_ms();
            if (wbec_ctx.power_loss_cnt > WBEC_POWER_LOSS_ATTEMPTS) {
                console_print_w_prefix("Reaching power loss limit, power off and go to standby now\r\n");
                // Чтобы включиться - нужно нажать кнопку или сбросить внешнее питание
                mcu_save_vcc_5v_last_state(MCU_VCC_5V_STATE_ON);
                mcu_goto_standby(WBEC_PERIODIC_WAKEUP_FIRST_TIMEOUT_S);
            } else {
                console_print_w_prefix("3.3V is lost, try to reset power\r\n");
                wbec_info.poweron_reason = REASON_PMIC_OFF;
                linux_pwr_hard_reset();
                new_state(WBEC_STATE_WAIT_POWER_ON);
            }
        }

        break;

    case WBEC_STATE_WAIT_POWER_OFF:
        // В этом состоянии ждем, пока логика управления питанием его выключит
        // Это занимает довольно много времени, т.к. выключение происходит через
        // сигнал PWRON, который надо активировать примерно на 6с
        if (!linux_pwr_is_busy()) {
            // После того как питание выключилось - засыпаем
            // Save VCC_5V state to RTC backup register
            if (wbec_ctx.powered_from_wbmz)
                mcu_save_vcc_5v_last_state(MCU_VCC_5V_STATE_OFF);
            else {
                mcu_save_vcc_5v_last_state(MCU_VCC_5V_STATE_ON);
            }
            mcu_goto_standby(WBEC_PERIODIC_WAKEUP_FIRST_TIMEOUT_S);
        }
        break;

    case WBEC_STATE_WAIT_LINUX_POWER_OFF:
        // В это состояние попадаем после короткого нажатия кнопки
        // Запрос на выключение в линукс уже отправлен (бит в регистре установлен)
        // Тут нужно подождать и если линукс не отправит запрос на
        // выключение питания - то выключить его принудительно

        if (linux_powerctrl_req == LINUX_POWERCTRL_OFF) {
            // Штатное выключение (запрос на выключение был с кнопки)
            console_print("\r\n\n");
            console_print_w_prefix("Power off request from Linux after power key pressed. Powering off...\r\n");
            linux_pwr_off();
            new_state(WBEC_STATE_WAIT_POWER_OFF);
        } else if (in_state_time() >= WBEC_LINUX_POWER_OFF_DELAY_MS) {
            // Аварийное выключение (можно как-то дополнительно обработать)
            console_print("\r\n\n");
            console_print_w_prefix("No power off request from Linux after power key pressed. Power is forced off.\r\n");
            linux_pwr_off();
            new_state(WBEC_STATE_WAIT_POWER_OFF);
        }

        break;
    }
}
