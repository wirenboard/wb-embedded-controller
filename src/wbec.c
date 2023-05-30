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

#define LINUX_POWERON_REASON(m) \
    m(REASON_POWER_ON,        "Wiren Board supply on"        ) \
    m(REASON_POWER_KEY,       "Power key pressed"            ) \
    m(REASON_RTC_ALARM,       "RTC alarm"                    ) \
    m(REASON_REBOOT,          "Reboot"                       ) \
    m(REASON_REBOOT_NO_ALARM, "Reboot instead of poweroff"   ) \
    m(REASON_WATCHDOG,        "Watchdog"                     ) \
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
    WBEC_STATE_WAIT_POWER_ON,
    WBEC_STATE_WORKING,
    WBEC_STATE_WAIT_POWER_OFF,

    WBEC_STATE_WAIT_LINUX_POWER_OFF,
    WBEC_STATE_WAIT_POWER_RESET,
};

struct wbec_ctx {
    enum mcu_poweron_reason mcu_poweron_reason;
    enum wbec_state state;
    systime_t timestamp;
    bool powered_from_wbmz;
};

static struct REGMAP_INFO wbec_info = {
    .wbec_id = WBEC_ID,
    .board_rev = 0,
    MODBUS_DEVICE_FW_VERSION_NUMBERS,
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
    case WBEC_STATE_WAIT_POWER_ON:          system_led_blink(50,  50);      break;
    case WBEC_STATE_WORKING:                system_led_blink(500, 1000);    break;
    case WBEC_STATE_WAIT_POWER_OFF:         system_led_blink(50,  50);      break;
    case WBEC_STATE_WAIT_LINUX_POWER_OFF:   system_led_blink(250, 250);     break;
    case WBEC_STATE_WAIT_POWER_RESET:       system_led_blink(50,  50);      break;
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
    adc->v_in = adc_get_ch_mv(ADC_CHANNEL_ADC_V_IN);
    adc->v_5_0 = adc_get_ch_mv(ADC_CHANNEL_ADC_5V);
    adc->v_3_3 = adc_get_ch_mv(ADC_CHANNEL_ADC_3V3);
    adc->vbus_console = adc_get_ch_mv(ADC_CHANNEL_ADC_VBUS_DEBUG);
    adc->vbus_network = adc_get_ch_mv(ADC_CHANNEL_ADC_VBUS_NETWORK);

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

    wbec_ctx.mcu_poweron_reason = mcu_get_poweron_reason();

    // Независимо от причины включения нужно измерить напряжение на линии 5В
    // Работаем на частоте 1 МГц для снижения потребления
    system_led_enable();
    rcc_set_hsi_1mhz_clock();
    system_led_disable();
    adc_init(ADC_CLOCK_DIV_64, ADC_VREF_INT);
    while (!adc_get_ready()) {};
    uint16_t vcc_5v = adc_get_ch_mv(ADC_CHANNEL_ADC_5V);
    bool vcc_5v_ok = (vcc_5v > 4700) && (vcc_5v < 5500);


    if (wbec_ctx.mcu_poweron_reason == MCU_POWERON_REASON_RTC_PERIODIC_WAKEUP) {
        // Если включилить от периодического пробуждения RTC,
        // значит мы попали сюда, выключившись при работе от WBMZ.
        // В этом состоянии отсутсвует питание на линии +5В и ЕС питаниется от BATSENSE
        // Напряжение питания может плавать от 2.7 до 3.3 В
        // Тут нужно ждать появления питания на линии +5В - измерять её относительно INT VREF

        if (vcc_5v_ok) {
            // Питание появилось - включаемся в обычном режиме
            // (просто идём дальше)
        } else {
            // Питание не появилось - засыпаем и ждём дальше
            // Здесь активен RTC periodic wakeup
            rtc_set_periodic_wakeup(2);
            mcu_goto_standby();
        }
    } else {
        // Во всех остальных случаях питание на линии +5В либо должно быть,
        // либо мы должны включить WBMZ, чтобы оно появилось
        // Проверяем, что питание есть
        if (vcc_5v_ok) {
            // Питание есть - включаемся в обычном режиме
            // (просто идём дальше)
            // WBMZ включится отдельным алторитмом, если Vin будет более 11.5 В
        } else {
            // Питания нет - включаем WBMZ
            linux_pwr_enable_wbmz();
        }
    }

    // Если дошли до этого места, надо включиться в обычном режиме
    rcc_set_hsi_pll_64mhz_clock();
    adc_init(ADC_CLOCK_DIV_64, ADC_VREF_EXT);
    rtc_disable_periodic_wakeup();
    new_state(WBEC_STATE_WAIT_STARTUP);
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
        if (vmon_ready()) {
            if (wbec_ctx.mcu_poweron_reason == MCU_POWERON_REASON_POWER_KEY) {
                // Если включились от кнопки,
                // проверим, что кнопка действительно нажата (прошла антидребезг)
                // чтобы не включаться от всяких помех и при отпускании кнопки
                if (pwrkey_ready()) {
                    if (pwrkey_pressed()) {
                        wbec_info.poweron_reason = REASON_POWER_KEY;
                        new_state(WBEC_STATE_VOLTAGE_CHECK);
                    } else {
                        mcu_goto_standby();
                    }
                }
            } else {
                // Если включились не от кнопки - сразу переходим дальше
                if (wbec_ctx.mcu_poweron_reason == MCU_POWERON_REASON_RTC_ALARM) {
                    wbec_info.poweron_reason = REASON_RTC_ALARM;
                } else if (wbec_ctx.mcu_poweron_reason == MCU_POWERON_REASON_POWER_ON) {
                    wbec_info.poweron_reason = REASON_POWER_ON;
                }
                new_state(WBEC_STATE_VOLTAGE_CHECK);
            }
        }
        break;

    case WBEC_STATE_VOLTAGE_CHECK:
        // В этом состоянии линукс всё ещё выключен
        // Тут нужно проверить напряжения и температуру (в будущем)
        // Также возможна ситуация, когда при обновлении прошивки МК перезагружается,
        // а линукс в это время работает. Тогда его не надо перезагружать.
        // Факт работы линукса определяется по наличию +3.3В
        if ((wbec_ctx.mcu_poweron_reason == MCU_POWERON_REASON_POWER_ON) && (vmon_get_ch_status(VMON_CHANNEL_V33))) {
            // Если после включения МК +3.3В есть - значить линукс уже работает
            // Не нужно выводить информацию в уарт
            // Тут ничего не нужно делать
            // Инициализируем GPIO управления питанием сразу во включенном состоянии
            linux_pwr_init(1);
        } else {
            // В ином случае перехватываем питание (выключаем)
            // И отправляем информацию в уарт
            linux_pwr_init(0);

            // Блокирующая отправка здесь - не страшно,
            // т.к. устройство в этом состоянии больше ничего не делает
            usart_tx_str_blocking("Wirenboard Embedded Controller\r\n");
            usart_tx_str_blocking("Version: ");
            usart_tx_buf_blocking(fwver_chars, ARRAY_SIZE(fwver_chars));
            usart_tx_str_blocking("\r\n");
            usart_tx_str_blocking("Git info: ");
            usart_tx_str_blocking(MODBUS_DEVICE_GIT_INFO);
            usart_tx_str_blocking("\r\n\n");

            usart_tx_str_blocking("Power on reason: ");
            usart_tx_str_blocking(get_poweron_reason_string(wbec_info.poweron_reason));
            usart_tx_str_blocking("\r\n\n");

            // TODO Display voltages and temp

            usart_tx_str_blocking("Power on now...\r\n\n");

            // После отправки данных включаем линукс
            linux_pwr_on();
        }

        // Перед включеним питания сбросим события с кнопки
        pwrkey_handle_short_press();
        pwrkey_handle_long_press();
        // Установим время WDT и запустим его
        wdt_set_timeout(WDEC_WATCHDOG_INITIAL_TIMEOUT_S);
        // TODO: uncomment
        // wdt_start_reset();

        // Выключим таймер периодического пробуждения, т.к.
        // дальше мы уже 100% включаемся
        rtc_disable_periodic_wakeup();

        // Заполним стуктуру INFO данными
        wbec_info.board_rev = fix16_to_int(adc_get_ch_adc_raw(ADC_CHANNEL_ADC_HW_VER));
        regmap_set_region_data(REGMAP_REGION_INFO, &wbec_info, sizeof(wbec_info));

        new_state(WBEC_STATE_WAIT_POWER_ON);
        break;

    case WBEC_STATE_WAIT_POWER_ON:
        // В этом состоянии ждём, пока логика включения питания его включит
        if (!linux_pwr_is_busy()) {
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
            wbec_ctx.state = WBEC_STATE_WAIT_LINUX_POWER_OFF;
            wbec_ctx.timestamp = systick_get_system_time_ms();
        }

        if (linux_powerctrl_req == LINUX_POWERCTRL_OFF) {
            // Если прилетел запрос из линукса на выключение
            // Это была выполнена команда `poweroff` или `rtcwake -m off`
            // Не должно быть возможности программно выключить контроллер так, чтобы
            // нужно было ехать нажимать кнопку чтобы его включить обратно
            // Поэтому здесь нужно проверить наличие будильника и если он есть - выключиться
            // иначе - перезагрузиться
            usart_tx_str_blocking("\n\rPower off request from Linux.\r\n");
            bool wbmz = linux_pwr_is_powered_from_wbmz();
            bool alarm = rtc_alarm_is_alarm_enabled();
            usart_tx_str_blocking("Alarm status: ");
            if (alarm) {
                usart_tx_str_blocking("set\r\n");
            } else {
                usart_tx_str_blocking("not set\r\n");
            }
            usart_tx_str_blocking("Power status: ");
            if (wbmz) {
                usart_tx_str_blocking("powered from WBMZ\r\n");
            } else {
                usart_tx_str_blocking("powered from external supply\r\n");
            }

            // Выключаться можно только если есть будильник ИЛИ питание от WBMZ
            if (wbmz || alarm) {
                // Если выключаемся при работающем WBMZ - дополнительно заводим RC periodic wakeup
                // чтобы просыпаться и следить за появлением входного напряжения
                if (wbmz) {
                    usart_tx_str_blocking("\r\nPowered from WBMZ, power off and set RTC periodic wakeup timer");
                    wbec_ctx.powered_from_wbmz = true;
                }
                linux_pwr_off();
                new_state(WBEC_STATE_WAIT_POWER_OFF);
            } else {
                usart_tx_str_blocking("\r\nAlarm not set, reboot system instead of power off.\r\n\n");
                wbec_info.poweron_reason = REASON_REBOOT_NO_ALARM;
                linux_pwr_off();
                new_state(WBEC_STATE_WAIT_POWER_RESET);
            }
        } else if (linux_powerctrl_req == LINUX_POWERCTRL_REBOOT) {
            // Если запрос на перезагрузку - перезагружается
            wbec_info.poweron_reason = REASON_REBOOT;
            usart_tx_str_blocking("\r\nReboot request, reset power.\r\n\n");
            linux_pwr_off();
            new_state(WBEC_STATE_WAIT_POWER_RESET);
        } else if (linux_powerctrl_req == LINUX_POWERCTRL_PMIC_RESET) {
            wbec_info.poweron_reason = REASON_REBOOT;
            usart_tx_str_blocking("\r\nPMIC reset request, activate PMIC RESET line now\r\n\n");
            linux_pwr_reset_pmic();
            new_state(WBEC_STATE_WAIT_POWER_ON);
        }

        // Если сработал WDT - перезагружаемся по питанию
        if (wdt_handle_timed_out()) {
            wbec_info.poweron_reason = REASON_WATCHDOG;
            usart_tx_str_blocking("\r\nWatchdog is timed out, reset power.\r\n\n");
            linux_pwr_off();
            new_state(WBEC_STATE_WAIT_POWER_RESET);
        }

        break;

    case WBEC_STATE_WAIT_POWER_OFF:
        // В этом состоянии ждем, пока логика управления питанием его выключит
        // Это занимает довольно много времени, т.к. выключение происходит через
        // сигнал PWRON, который надо активировать примерно на 6с
        if (!linux_pwr_is_busy()) {
            // После того как питание выключилось - засыпаем
            // Если работали от WBMZ - заводим RTC periodic wakeup
            if (wbec_ctx.powered_from_wbmz) {
                usart_tx_str_blocking("\r\nPower off now, wake up to check voltages after 2 seconds\r\n");
                rtc_set_periodic_wakeup(10);
            }
            mcu_goto_standby();
        }
        break;

    case WBEC_STATE_WAIT_LINUX_POWER_OFF:
        // В это состояние попадаем после короткого нажатия кнопки
        // Запрос на выключение в линукс уже отправлен (бит в регистре установлен)
        // Тут нужно подождать и если линукс не отправит запрос на
        // выключение питания - то выключить его принудительно

        if (linux_powerctrl_req == LINUX_POWERCTRL_OFF) {
            // Штатное выключение (запрос на выключение был с кнопки)
            usart_tx_str_blocking("\r\nPower off request from Linux after power key pressed. Power is off.\r\n\n");
            linux_pwr_off();
            new_state(WBEC_STATE_WAIT_POWER_OFF);
        } else if (in_state_time() >= WBEC_LINUX_POWER_OFF_DELAY_MS) {
            // Аварийное выключение (можно как-то дополнительно обработать)
            usart_tx_str_blocking("\r\nNo power off request from Linux after power key pressed. Power is forced off.\r\n\n");
            linux_pwr_off();
            new_state(WBEC_STATE_WAIT_POWER_OFF);
        }

        break;

    case WBEC_STATE_WAIT_POWER_RESET:
        // В это состояние попадаем, когда нужно дернуть питание для перезагрузки
        // Ждем, пока отработает логика управления питанием (питание отключится),
        // затем ещё ждем время и переходим в начальное состояние

        if (linux_pwr_is_busy()) {
            wbec_ctx.timestamp = systick_get_system_time_ms();
        }

        if (in_state_time() >= WBEC_POWER_RESET_TIME_MS) {
            wbec_ctx.state = WBEC_STATE_VOLTAGE_CHECK;
        }

        break;
    }
}

