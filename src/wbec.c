#include "wbec.h"

#include <string.h>

#include "config.h"
#include "regmap-int.h"
#include "pwrkey.h"
#include "irq-subsystem.h"
#include "wdt.h"
#include "system-led.h"
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
#include "hwrev.h"
#include "buzzer.h"
#include "temperature-control.h"
#include "gpio-subsystem.h"
#include "wbmz-common.h"
#include "wdt-stm32.h"

#define LINUX_POWERON_REASON(m) \
    m(REASON_POWER_ON,        "Power supply on"        ) \
    m(REASON_POWER_KEY,       "Power button"                 ) \
    m(REASON_RTC_ALARM,       "RTC alarm"                    ) \
    m(REASON_REBOOT,          "Reboot"                       ) \
    m(REASON_REBOOT_NO_ALARM, "Reboot instead of poweroff"   ) \
    m(REASON_WATCHDOG,        "Watchdog"                     ) \
    m(REASON_PMIC_OFF,        "PMIC is unexpectedly off"     ) \
    m(REASON_UNKNOWN,         "Unknown"                      ) \
    /* Новые значения добавлять только в конец: значения - часть ABI regmap */ \
    m(REASON_WATCHDOG_WARM,   "Watchdog (warm reset)"        ) \

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
    enum linux_poweron_reason poweron_reason;
    // Была ли уже попытка тёплого сброса по watchdog.
    // Сбрасывается, когда Linux сбрасывает watchdog (система жива).
    // Если после тёплого сброса Linux так и не начал сбрасывать watchdog,
    // следующий таймаут приведёт к жёсткому сбросу по питанию
    bool wd_warm_reset_attempted;
    // Режим suspend: Linux объявил окно сна через SUSPEND_CTRL.
    // В этом режиме потеря 3.3В ожидаема (BL31 гасит DCDC1), а
    // watchdog-эскалация заменена на дедлайн запрошенной длительности.
    bool suspend_mode;
    systime_t suspend_entry_timestamp;
    uint32_t suspend_timeout_ms;
    // Взводится, когда во время suspend реально пропало 3.3В.
    // До этого момента кормления watchdog НЕ завершают режим:
    // между записью в SUSPEND_CTRL и заморозкой системы демон
    // watchdog продолжает кормить ещё ~1-2 секунды.
    bool suspend_started;
    // Режим suspend-to-off: BL31 усыпляет PMIC целиком (остаётся
    // только питание DRAM). Пробуждение делает EC: по будильнику
    // (или дедлайну/кнопке) перезапускает PMIC импульсом PWRON.
    bool suspend_off_mode;
    // suspend-to-off Stop 1: окно сна вооружено (EXTI-источники заведены,
    // выходы переведены в безопасное состояние, WUT арм-нут один раз).
    // Живёт в SRAM через Stop (сброса нет); разоружается на выходе.
    bool stop_armed;
    // Момент вооружения окна (systick). ≈ момент, когда BL31 снял рельсы PMIC
    // и взвёл его сон (arm взводится по первому же пропаданию 3.3В). Служит
    // якорем минимальной паузы «арм -> первый PWRON» на пути пробуждения:
    // при мгновенном выходе из окна systick с этого момента идёт непрерывно
    // (Stop реально не спал), поэтому пауза отмеряется точно.
    systime_t stop_armed_ts;
    // Счётчик истёкших периодов WUT за окно (systick в Stop заморожен, поэтому
    // дедлайн считаем по тикам WUT, а не по systick). Сбрасывается при арме.
    uint32_t suspend_wut_ticks;
    // Проснулись по фронту кнопки, короткое нажатие ещё не подтверждено
    // антидребезгом: остаёмся бодрствовать (не уходим в Stop), пока pwrkey
    // не разберёт нажатие или не истечёт окно ожидания.
    bool stop_button_wake;
    systime_t stop_button_wake_ts;
    // Кнопка физически удерживалась в момент выхода из окна сна: её отпускание
    // придёт уже после resume и НЕ должно породить новое «короткое нажатие»
    // (в !linux_booted-ветке это немедленный hard off - стенд 2026-07-09).
    // Глотаем события кнопки до наблюдаемого отпускания.
    bool pwrkey_swallow;
    systime_t pwrkey_swallow_ts;
    // Пробуждение из окна suspend-to-off: Linux НЕ загружается заново, а
    // ВОЗОБНОВЛЯЕТСЯ (DRAM жива). Завершение последовательности включения
    // обязано сохранить linux_booted=true - иначе первые 20 с после resume
    // короткое нажатие попадает в ветку «линукс не загружен» и жёстко рубит
    // питание (стенд 2026-07-09 17:33). Холодные включения (standby-poweroff,
    // подача питания, watchdog-сбросы) этот флаг не взводят.
    bool suspend_wake_resume;
    // Предыдущий антидребезженный уровень кнопки: IRQ_PWR_OFF_REQ взводится
    // по ФРОНТУ нажатия, один раз на одно физическое нажатие. Раньше флаг
    // взводился каждый проход, пока кнопка удержана: Linux успевал квитировать
    // IRQ, и следующий проход взводил его снова - одно нажатие доезжало до
    // драйвера дважды (стенд 2026-07-09 17:34, двойной "PWR_OFF_REQ latched").
    bool pwrkey_prev_level;
    // Взводится при пробуждении из suspend-to-off, чтобы подавить
    // звуковой сигнал включения на этом входе в WBEC_STATE_WORKING.
    // ПОЧЕМУ это критично: resume из suspend-to-off — это ре-инициализация
    // DRAM поверх самообновления (содержимое памяти сохраняется), процесс
    // очень чувствителен к возмущениям по питанию/таймингу. Пищалка,
    // сработавшая в этот момент, срывает resume, и плата уходит в
    // холодную перезагрузку вместо пробуждения (проверено на железе
    // 2026-07-07: включённый бип на resume → cold-boot; выключенный → ок).
    // Обычное включение (холодный старт/перезагрузка) инициализирует DRAM
    // с нуля и к сигналу не чувствительно — там бип нужен и безопасен.
    bool suspend_resume_no_beep;
};

static struct wbec_ctx wbec_ctx;

void __attribute__((weak)) linux_poweron_handler(void) {}

static void new_state(enum wbec_state s)
{
    wbec_ctx.state = s;
    wbec_ctx.timestamp = systick_get_system_time_ms();

    switch (wbec_ctx.state) {
    case WBEC_STATE_WAIT_STARTUP:               system_led_blink(5,   100);     break;
    case WBEC_STATE_VOLTAGE_CHECK:              system_led_blink(5,   100);     break;
    case WBEC_STATE_TEMP_CHECK_LOOP:            system_led_blink(5,   100);     break;
    case WBEC_STATE_POWER_ON_SEQUENCE_WAIT:     system_led_blink(50,  50);      break;
    case WBEC_STATE_POWER_OFF_SEQUENCE_WAIT:    system_led_blink(50,  50);      break;
    default:                                    system_led_enable();            break; // GCOVR_EXCL_LINE

    case WBEC_STATE_WORKING:
        system_led_blink(500, 1000);
        // Сигнал включения — на РЕАЛЬНОМ включении (холодный старт/перезагрузка),
        // но НЕ на resume из suspend-to-off: там пищалка возмущает ре-инициализацию
        // DRAM поверх самообновления и плата уходит в cold-boot вместо пробуждения
        // (проверено на железе). Флаг взводится на пути пробуждения suspend-to-off.
        if (!wbec_ctx.suspend_resume_no_beep) {
            buzzer_beep(EC_BUZZER_BEEP_FREQ, EC_BUZZER_BEEP_POWERON_MS);
        }
        wbec_ctx.suspend_resume_no_beep = false;
        linux_poweron_handler();
        break;
    }
}

static inline systime_t in_state_time_ms(void)
{
    return systick_get_time_since_timestamp(wbec_ctx.timestamp);
}

static inline const char * get_poweron_reason_string(enum linux_poweron_reason r)
{
    if (r >= ARRAY_SIZE(linux_power_reason_strings)) {
        return "Unknown"; // GCOVR_EXCL_LINE
    }
    return linux_power_reason_strings[r];
}

static inline void collect_adc_data(struct REGMAP_ADC_DATA * adc)
{
    // При питании от WBMZ не нужно учитывать падение на диоде
    if (wbmz_is_powered_from_wbmz()) {
        adc_set_offset_mv(ADC_CHANNEL_ADC_V_IN, 0);
    } else {
        adc_set_offset_mv(ADC_CHANNEL_ADC_V_IN, ADC_V_IN_DIODE_DROP_MV);
    }

    // Get voltages
    adc->v_a1 = adc_get_ch_mv(ADC_CHANNEL_ADC_IN1);
    adc->v_a2 = adc_get_ch_mv(ADC_CHANNEL_ADC_IN2);
    adc->v_a3 = adc_get_ch_mv(ADC_CHANNEL_ADC_IN3);
    adc->v_a4 = adc_get_ch_mv(ADC_CHANNEL_ADC_IN4);
    adc->v_5_0 = adc_get_ch_mv(ADC_CHANNEL_ADC_5V);
    adc->v_3_3 = adc_get_ch_mv(ADC_CHANNEL_ADC_3V3);
    adc->vbus_console = adc_get_ch_mv(ADC_CHANNEL_ADC_VBUS_DEBUG);
    #if defined(EC_USB_HUB_DEBUG_NETWORK)
        adc->vbus_network = 0;
    #else
        adc->vbus_network = adc_get_ch_mv(ADC_CHANNEL_ADC_VBUS_NETWORK);
    #endif

    if (vmon_get_ch_status(VMON_CHANNEL_V_IN)) {
        adc->v_in = adc_get_ch_mv(ADC_CHANNEL_ADC_V_IN);
    } else {
        adc->v_in = 0;
    }

    adc->temp = temperature_control_get_temperature_c_x100();
}

static inline enum linux_powerctrl_req get_linux_powerctrl_req(void)
{
    enum linux_powerctrl_req ret = LINUX_POWERCTRL_NO_ACTION;

    struct REGMAP_POWER_CTRL p;
    if (regmap_get_data_if_region_changed(REGMAP_REGION_POWER_CTRL, &p, sizeof(p))) {
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

    enum mcu_poweron_reason mcu_poweron_reason = mcu_get_poweron_reason();
    wbec_ctx.poweron_reason = REASON_UNKNOWN;

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
        wbec_ctx.poweron_reason = REASON_POWER_ON;
        break;

    case MCU_POWERON_REASON_POWER_KEY:
        // Если включились от кнопки
        // Нужно проверить на антидребезг и включиться в обычном режиме
        // (просто идём дальше)
        while (!pwrkey_ready()) {
            pwrkey_do_periodic_work();
            watchdog_reload();
        }
        if (pwrkey_pressed()) {
            wbec_ctx.poweron_reason = REASON_POWER_KEY;
        } else {
            // Если кнопка не нажата (или нажата коротко и не прошла антидребезг) - засыпаем
            linux_cpu_pwr_seq_off_and_goto_standby(WBEC_PERIODIC_WAKEUP_NEXT_TIMEOUT_S);
        }
        break;

    case MCU_POWERON_REASON_RTC_ALARM:
        // Если включились от будильника
        // Нужно включиться в обычном режиме
        // (просто идём дальше)
        wbec_ctx.poweron_reason = REASON_RTC_ALARM;
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
                wbec_ctx.poweron_reason = REASON_POWER_ON;
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
        wbmz_enable_stepup();
    }

    // Если дошли до этого места, надо включиться в обычном режиме
    rtc_disable_periodic_wakeup();
    new_state(WBEC_STATE_WAIT_STARTUP);
}

void wbec_do_periodic_work(void)
{
    struct REGMAP_ADC_DATA adc;
    struct REGMAP_POWERON_REASON poweron_reason;
    collect_adc_data(&adc);
    regmap_set_region_data(REGMAP_REGION_ADC_DATA, &adc, sizeof(adc));
    poweron_reason.poweron_reason = wbec_ctx.poweron_reason;
    regmap_set_region_data(REGMAP_REGION_POWERON_REASON, &poweron_reason, sizeof(poweron_reason));

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
            if ((wbec_ctx.poweron_reason == REASON_POWER_ON) && (vmon_get_ch_status(VMON_CHANNEL_V33))) {
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
        if (wbec_ctx.poweron_reason == REASON_POWER_ON) {
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
        console_print(get_poweron_reason_string(wbec_ctx.poweron_reason));
        console_print("\r\n");

        console_print_w_prefix("RTC time: ");
        console_print_time_now();
        console_print("\r\n");

        console_print_w_prefix("Board temperature: ");
        console_print_fixed_point(adc.temp / 10, 1);
        console_print("ºC\r\n");

        console_print_w_prefix("Vin: ");
        console_print_fixed_point(adc.v_in / 100, 1);
        #if defined EC_USB_HUB_DEBUG_NETWORK
            console_print("V, USB: ");
            console_print_fixed_point(adc.vbus_console / 100, 1);
            console_print("V\r\n");
        #else
            console_print("V, USB Net: ");
            console_print_fixed_point(adc.vbus_network / 100, 1);
            console_print("V, USB Console: ");
            console_print_fixed_point(adc.vbus_console / 100, 1);
            console_print("V\r\n");
        #endif

        if (temperature_control_is_temperature_ready()) {
            console_print_w_prefix("Turning on the main CPU; all future debug messages will originate from the CPU.\r\n\n\n");
            // После отправки данных включаем линукс
            linux_cpu_pwr_seq_on();
            new_state(WBEC_STATE_POWER_ON_SEQUENCE_WAIT);
        } else {
            console_print_w_prefix("WARNING: Board temperature is too low!\r\n");
            new_state(WBEC_STATE_TEMP_CHECK_LOOP);
        }

        break;

    case WBEC_STATE_TEMP_CHECK_LOOP:
        // В этом состоянии линукс выключен, проверяем температуру
        // Сидим тут до тех пор, пока температура не станет выше -40
        if (in_state_time_ms() > 5000) {
            if (temperature_control_is_temperature_ready()) {
                console_print_w_prefix("Temperature is OK!\r\n");
                console_print_w_prefix("Turning on the main CPU; all future debug messages will originate from the CPU\r\n\n\n");
                linux_cpu_pwr_seq_on();
                new_state(WBEC_STATE_POWER_ON_SEQUENCE_WAIT);
            } else {
                console_print_w_prefix("Current board temperature ");
                console_print_fixed_point(adc.temp / 10, 1);
                console_print("ºC is too low! Waiting for it to rise above ");
                console_print_fixed_point(WBEC_MINIMUM_WORKING_TEMPERATURE * 10, 1);
                console_print("ºC\r\n");
                new_state(WBEC_STATE_TEMP_CHECK_LOOP);
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
            // В этом случае считаем его уже загруженным.
            // suspend_wake_resume: пробуждение из suspend-to-off - Linux
            // ВОЗОБНОВЛЯЕТСЯ, а не загружается, поэтому остаётся «загруженным»
            // (иначе нажатие в первые 20 с после resume жёстко рубит питание).
            wbec_ctx.linux_booted = wbec_ctx.linux_initial_powered_on ||
                                    wbec_ctx.suspend_wake_resume;
            wbec_ctx.linux_initial_powered_on = false;
            wbec_ctx.suspend_wake_resume = false;
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

        if (wbec_ctx.pwrkey_swallow) {
            // Хвост нажатия, пережившего окно сна: глотаем «короткое нажатие»
            // до наблюдаемого отпускания. Нажатие могло быть ещё НЕ
            // подтверждённым на выходе из окна (сырой уровень нажат, антидребезг
            // не истёк) - тогда антидребезженный уровень читается «отпущено» ещё
            // до PWRKEY_DEBOUNCE_MS после реального начала нажатия. Поэтому
            // снимаем глотание только после того, как уровень непрерывно
            // «отпущено» дольше антидребезга с запасом - к этому моменту сырое
            // нажатие «в полёте» невозможно. Долгое нажатие (принудительное
            // выключение) обрабатывается раньше по циклу в
            // linux_cpu_pwr_seq_do_periodic_work и не глотается.
            (void)pwrkey_handle_short_press();
            if (pwrkey_pressed()) {
                wbec_ctx.pwrkey_swallow_ts = systick_get_system_time_ms();
            } else if (systick_get_time_since_timestamp(wbec_ctx.pwrkey_swallow_ts) >
                       (PWRKEY_DEBOUNCE_MS + 200)) {
                wbec_ctx.pwrkey_swallow = false;
            }
        } else if (wbec_ctx.linux_booted) {
            // Если линукс загружен - отправляем запрос на выключение
            // При этом не ждём полноценного нажатия, а отправляем запрос сразу
            // Если выполняется долгое нажатие, то есть шанс что линукс успеет
            // корректно выключиться
            if (pwrkey_pressed()) {
                if (!wbec_ctx.pwrkey_prev_level) {
                    // Только ФРОНТ нажатия: один IRQ на одно физическое
                    // нажатие. Взводить каждый проход нельзя: Linux квитирует
                    // IRQ, пока кнопка ещё удержана, и то же нажатие доезжает
                    // до драйвера вторым событием (стенд 2026-07-09 17:34).
                    irq_set_flag(IRQ_PWR_OFF_REQ);
                }
                wbec_ctx.pwrkey_pressed = true;
                wbec_ctx.pwrkey_pressed_timestamp = systick_get_system_time_ms();
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
        // Трек фронта кнопки для взведения IRQ (обновляется в любом состоянии
        // ветки выше, включая «глотание», чтобы фронт не был ложным после него)
        wbec_ctx.pwrkey_prev_level = pwrkey_pressed();

        // Если флаг нажатой кнопки висит слишком долго (линукс по каким-то причинам не отреагировал)
        // Нужно его сбросить, т.к. он влияет на решение для выключения по poweroff
        if (wbec_ctx.pwrkey_pressed) {
            if (systick_get_time_since_timestamp(wbec_ctx.pwrkey_pressed_timestamp) > WBEC_LINUX_POWER_OFF_DELAY_MS) {
                wbec_ctx.pwrkey_pressed = false;
            }
        }

        // Запрос режима suspend из Linux: запись таймаута (секунды) в
        // SUSPEND_CTRL включает режим, запись 0 - выключает.
        {
            struct REGMAP_SUSPEND_CTRL s;
            if (regmap_get_data_if_region_changed(REGMAP_REGION_SUSPEND_CTRL, &s, sizeof(s))) {
                if (s.timeout_s > 0) {
                    wbec_ctx.suspend_mode = true;
                    wbec_ctx.suspend_started = false;
                    wbec_ctx.suspend_off_mode = s.off_mode;
                    wbec_ctx.suspend_entry_timestamp = systick_get_system_time_ms();
                    // Запас 10 с поверх запрошенной длительности
                    wbec_ctx.suspend_timeout_ms = (uint32_t)s.timeout_s * 1000 + 10000;
                    if (wbec_ctx.suspend_off_mode) {
                        // Сбрасываем возможное залипшее событие будильника перед
                        // входом в off-mode. Флаг ALRAF в RTC (домен резервного
                        // питания) и программная защёлка alarm_fired_latch
                        // переживают сброс/пропадание питания и не сбрасываются
                        // нигде, кроме off-mode. Будильник, разбудивший плату в
                        // прошлый раз, оставляет их взведёнными; без сброса первый
                        // же опрос off-mode прочитает это как свежее срабатывание и
                        // разбудит плату немедленно (~220 мс) вместо запрошенного
                        // времени. Сбрасываем обе защёлки, чтобы разбудил только
                        // свежий будильник (или дедлайн/кнопка).
                        rtc_alarm_take_fired();
                        rtc_clear_alarm_flag();
                        console_print_w_prefix("Suspend mode: on (power-off, wake by alarm)\r\n");
                    } else {
                        console_print_w_prefix("Suspend mode: on\r\n");
                    }
                } else if (wbec_ctx.suspend_mode) {
                    wbec_ctx.suspend_mode = false;
                    // Разоружаем «сон начался» при выходе из режима, чтобы на
                    // следующем цикле кормление watchdog не завершило suspend
                    // немедленно (флаг живёт в ЕС между циклами - тёплый сброс
                    // и пробуждение по будильнику не перезапускают ЕС).
                    wbec_ctx.suspend_started = false;
                    console_print_w_prefix("Suspend mode: off (request)\r\n");
                }
            }
        }

        if (linux_powerctrl_req == LINUX_POWERCTRL_OFF) {
            // Если прилетел запрос из линукса на выключение
            // Это была выполнена команда `poweroff` или `rtcwake -m off`
            console_print("\r\n\n");
            console_print_w_prefix("Power off request from Linux.\r\n");
            bool wbmz = wbmz_is_powered_from_wbmz();
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
                wbec_ctx.poweron_reason = REASON_REBOOT_NO_ALARM;
                linux_cpu_pwr_seq_hard_reset();
                new_state(WBEC_STATE_POWER_ON_SEQUENCE_WAIT);
            }
        } else if (linux_powerctrl_req == LINUX_POWERCTRL_REBOOT) {
            // Если запрос на перезагрузку - перезагружается
            wbec_ctx.poweron_reason = REASON_REBOOT;
            console_print("\r\n\n");
            console_print_w_prefix("Reboot request, reset power.\r\n");
            linux_cpu_pwr_seq_hard_reset();
            new_state(WBEC_STATE_POWER_ON_SEQUENCE_WAIT);
        } else if (linux_powerctrl_req == LINUX_POWERCTRL_PMIC_RESET) {
#if defined(WBEC_HAS_WARM_RESET)
            // Тёплый сброс SoC по запросу из Linux: короткий импульс на линии
            // PWROK/RESET. Питание PMIC (и DRAM) не отключается, поэтому
            // сохранённые в DRAM логи (ramoops) переживают перезагрузку
            wbec_ctx.poweron_reason = REASON_REBOOT;
            console_print("\r\n\n");
            console_print_w_prefix("Warm reset request, pulse PMIC RESET (PWROK) line now\r\n\n");
            linux_cpu_pwr_seq_warm_reset();
            new_state(WBEC_STATE_POWER_ON_SEQUENCE_WAIT);
#else
            // Историческое поведение: сброс PMIC через линию RESET (до 2 с)
            wbec_ctx.poweron_reason = REASON_REBOOT;
            console_print("\r\n\n");
            console_print_w_prefix("PMIC reset request, activate PMIC RESET line now\r\n\n");
            linux_cpu_pwr_seq_reset_pmic();
            new_state(WBEC_STATE_POWER_ON_SEQUENCE_WAIT);
#endif
        }

        // За один проход разрешено только одно действие с питанием.
        // Если запрос из Linux уже начал смену состояния, проверки ниже
        // (watchdog, контроль 3.3В) выполнять нельзя: их обработчики
        // запускают свои последовательности поверх уже начатой и могут
        // прервать её (например, оставить линию PMIC RESET (PWROK)
        // взведённой навсегда - SoC заклинит в сбросе).
        // Взведённые флаги watchdog не теряются: они будут обработаны
        // после завершения начатой последовательности
        if (wbec_ctx.state != WBEC_STATE_WORKING) {
            break;
        }

        // Suspend-to-off: питание SoC выключено самим PMIC (кроме DRAM), Linux
        // заморожен в самообновляющейся памяти. Как только сон реально начался
        // (3.3В пропало -> suspend_started), EC уходит в STM32 Stop 1 между
        // пробуждениями, вместо того чтобы жечь ток на 64 МГц весь сон. Stop
        // сохраняет SRAM и защёлки GPIO и просыпается на месте (сброса НЕТ),
        // поэтому провала на линиях PMIC, погубившего Standby-подход, здесь нет
        // по построению. Пробуждаемся по будильнику (запрос Linux), дедлайну
        // (бэкстоп через тики RTC WUT) или кнопке (фронт EXTI). Классификация —
        // в начале блока: потребляем защёлки, которые драйверы супер-цикла
        // (pwrkey/rtc-alarm) успели выставить в этом же проходе. WFI — в самом
        // низу; после пробуждения возвращаемся в супер-цикл. См.
        // ec-stop-mode-brief.md.
        if (wbec_ctx.suspend_mode && wbec_ctx.suspend_off_mode &&
            wbec_ctx.suspend_started) {
            // Однократное вооружение окна при первом входе (идемпотентно).
            if (!wbec_ctx.stop_armed) {
                // Управляемые супер-циклом выходы — в безопасное состояние на
                // всё окно: их мониторинг (NTC, V_IN) в Stop заморожен, а
                // защёлки GPIO сохраняются (см. brief §6).
                temperature_control_suspend(true);     // PD2: нагреватель off
                gpio_suspend(true);                    // PD0: V_OUT off
                // WUT арм-нут ОДИН раз: он кормит IWDG (< 10 с) и служит тиком
                // дедлайна. Повторный арм каждый цикл входил бы в INIT-режим
                // RTC и останавливал календарь — единственную базу времени Stop.
                rtc_set_periodic_wakeup(WBEC_SUSPEND_STOP_FEED_PERIOD_S);
                mcu_stop_window_prepare();             // EXTI/NVIC + маска SoC-CS
                wbec_ctx.suspend_wut_ticks = 0;
                wbec_ctx.stop_button_wake = false;
                wbec_ctx.stop_armed = true;
                // Якорь минимальной паузы «арм -> первый PWRON» (см. пробуждение).
                wbec_ctx.stop_armed_ts = systick_get_system_time_ms();
                // Диагностика: помечаем РЕАЛЬНОЕ начало окна сна. Между
                // объявлением (0xA4) и пропаданием 3.3В SoC может входить в
                // suspend десятки секунд; без этой строки по логу стенда не
                // отличить «EC ещё не спал» от «EC проснулся мгновенно»
                // (ложный диагноз 2026-07-08).
                console_print("\r\n\n");
                console_print_w_prefix("Suspend-to-off: EC sleeping in Stop 1 (wake: alarm/button/deadline)\r\n");
            }

            // Классификация пробуждения.
            bool alarm = rtc_alarm_take_fired();
            // Кнопка: пробуждение по ПОДТВЕРЖДЕНИЮ нажатия - антидребезженный
            // уровень «нажато» (+500 мс удержания) после фронта, разбудившего
            // EC, - а НЕ по отпусканию. UX-контракт (Evgeny, 2026-07-09):
            // «нажал - услышал бип (кнопка ещё в руке) - отпустил». Бип звучит
            // на возврате 3.3В (~+0.3 с после подтверждения), пока держат.
            // Резервно потребляем и защёлку короткого нажатия - отпускание,
            // успевшее пройти целиком (защёлка не должна пережить окно).
            bool button = (wbec_ctx.stop_button_wake && pwrkey_pressed()) ||
                          pwrkey_handle_short_press();
            // Дедлайн — по накопленным тикам WUT, НЕ по systick (в Stop мёртв):
            // suspend_timeout_ms уже с запасом +10 с (см. запись SUSPEND_CTRL).
            bool deadline = ((uint64_t)wbec_ctx.suspend_wut_ticks *
                             WBEC_SUSPEND_STOP_FEED_PERIOD_S * 1000) >=
                            wbec_ctx.suspend_timeout_ms;

            if (alarm || button || deadline) {
                // Нажатие «в полёте» на момент выхода: уровень уже нажат ИЛИ
                // фронт видели, но антидребезг ещё не подтвердил (пробуждение
                // по будильнику/дедлайну посреди начинающегося нажатия). Его
                // подтверждение и отпускание придут ПОСЛЕ выхода и должны быть
                // проглочены (см. pwrkey_swallow ниже).
                bool press_in_flight = pwrkey_pressed() || wbec_ctx.stop_button_wake;
                wbec_ctx.suspend_mode = false;
                wbec_ctx.suspend_off_mode = false;
                // Разоружаем «сон начался»: пробуждение - это resume того же
                // ядра, ЕС не перезапускается, поэтому без сброса флаг остался
                // бы взведён и на следующем цикле первое же кормление watchdog
                // завершило бы suspend немедленно.
                wbec_ctx.suspend_started = false;
                wbec_ctx.stop_armed = false;
                wbec_ctx.stop_button_wake = false;
                // Это resume того же ядра (DRAM в самообновлении): бип на входе
                // в WORKING сорвёт ре-инициализацию DRAM и уронит плату в
                // cold-boot. Подавляем сигнал включения (см. new_state()).
                wbec_ctx.suspend_resume_no_beep = true;
                // Linux возобновляется, а не загружается: сохранить
                // linux_booted через завершение последовательности включения.
                wbec_ctx.suspend_wake_resume = true;
                if (alarm) {
                    wbec_ctx.poweron_reason = REASON_RTC_ALARM;
                } else if (button) {
                    wbec_ctx.poweron_reason = REASON_POWER_KEY;
                    // Требование UX: нажатие в окне сна -> бип + пробуждение.
                    // Пищать ЗДЕСЬ бессмысленно: пищалка (SG1) запитана от
                    // 3.3В, которое во время окна ВЫКЛЮЧЕНО (стенд 2026-07-09:
                    // вызов buzzer_beep с валидными параметрами - и тишина,
                    // диагностическая сборка 93d307e). Бип
                    // откладывается в последовательность пробуждения: он
                    // прозвучит в первый момент возврата 3.3В, пока SoC ещё в
                    // сбросе (DRAM в самообновлении, ре-инициализация не
                    // начата - безопасно, урок f3face8), а PWROK - после бипа.
                    // Бип на входе в WORKING остаётся ПОДАВЛЕННЫМ
                    // (suspend_resume_no_beep). Будильник и дедлайн будят
                    // МОЛЧА - необслуживаемые пробуждения.
                    linux_cpu_pwr_seq_wake_beep_request();
                } else {
                    wbec_ctx.poweron_reason = REASON_WATCHDOG;
                }
                // Нажатие кнопки в окне сна потреблено как ПРИЧИНА ПРОБУЖДЕНИЯ
                // (или случилось, пока Linux был обесточен) - гасим его признаки
                // для Linux. Блок «linux_booted -> irq_set_flag(IRQ_PWR_OFF_REQ)»
                // выше по WORKING отработал и для этого нажатия (для EC Linux
                // «включён» весь suspend), флаг никем не квитируется (Linux
                // спит) и доехал бы до драйвера wbec-pwrkey после resume вторым
                // событием: одно нажатие = пробуждение + poweroff от logind +
                // cold boot (стенд 2026-07-08). Нажатие в обычной работе
                // по-прежнему доставляется как раньше: этот сброс происходит
                // только на выходе из окна suspend-to-off. То же скрытое
                // поведение есть и на базовой a655128 (busy-poll) - см. коммит.
                wbec_ctx.pwrkey_pressed = false;
                irq_clear_flags(1u << IRQ_PWR_OFF_REQ);
                // Если нажатие ещё «в полёте» (кнопка удерживается - теперь
                // это ШТАТНЫЙ случай кнопочного пробуждения-по-подтверждению, -
                // или фронт видели без подтверждения), его подтверждение/
                // отпускание придёт после resume: глотаем события кнопки до
                // наблюдаемого отпускания (см. WORKING).
                wbec_ctx.pwrkey_swallow = press_in_flight;
                wbec_ctx.pwrkey_swallow_ts = systick_get_system_time_ms();
                // Возвращаем выходы под штатное управление и снимаем маску
                // SoC-CS EXTI; резервный WUT-дедлайн больше не нужен.
                temperature_control_suspend(false);
                gpio_suspend(false);
                mcu_stop_window_finish();
                rtc_disable_periodic_wakeup();
                console_print("\r\n\n");
                console_print_w_prefix("Suspend-to-off: wake up, restart PMIC via PWRON\r\n");
                linux_cpu_pwr_seq_wakeup();
                // Мгновенный выход из окна (дедлайн-тиков WUT не было -> Stop
                // реально не спал): между тем, как BL31 усыпил PMIC, и этим
                // моментом прошли лишь ~0.1-0.2 с; PWRON, прилетевший в
                // переходный процесс PMIC, глотается (стенд 2026-07-09).
                // Откладываем первый PWRON до stop_armed_ts + MIN_DELAY, чтобы
                // PMIC успел завершить вход в сон. При реальном сне (ticks > 0)
                // переход давно завершён - паузу не запрашиваем.
                if (wbec_ctx.suspend_wut_ticks == 0) {
                    linux_cpu_pwr_seq_wake_pwron_min_delay(wbec_ctx.stop_armed_ts);
                }
                new_state(WBEC_STATE_POWER_ON_SEQUENCE_WAIT);
                break;
            }

            // Пробуждение было фронтом кнопки, но короткое нажатие ещё не прошло
            // антидребезг (500 мс, нужен непрерывный systick). Остаёмся в
            // бодрствующем супер-цикле, чтобы pwrkey_do_periodic_work разобрал
            // нажатие штатно, и НЕ уходим в Stop, пока не истечёт окно ожидания.
            // Так сохраняется фильтр случайных касаний (в отличие от «будим по
            // самому фронту»), ценой ~1 с бодрствования на событие.
            if (mcu_stop_take_button_wake()) {
                wbec_ctx.stop_button_wake = true;
                wbec_ctx.stop_button_wake_ts = systick_get_system_time_ms();
            }
            if (wbec_ctx.stop_button_wake) {
                if (pwrkey_pressed()) {
                    // Кнопка ещё УДЕРЖИВАЕТСЯ (антидребезженный уровень):
                    // продлеваем окно ожидания - короткое нажатие фиксируется
                    // только на ОТПУСКАНИИ. Иначе удержание дольше grace-окна
                    // (~1.2 с) роняло EC обратно в Stop посреди нажатия
                    // (стенд 2026-07-09 14:46: 4 подряд брошенных нажатия).
                    wbec_ctx.stop_button_wake_ts = systick_get_system_time_ms();
                    break;
                }
                if (systick_get_time_since_timestamp(wbec_ctx.stop_button_wake_ts) <
                    (PWRKEY_DEBOUNCE_MS + WBEC_SUSPEND_STOP_BUTTON_GRACE_MS)) {
                    break;      // остаёмся бодрствовать, ждём разбор нажатия
                }
                // Нажатие не подтвердилось за окно ожидания — снова спим.
                wbec_ctx.stop_button_wake = false;
            }

            // Реальной причины нет: кормим IWDG и уходим в Stop до следующего
            // тика WUT / будильника / кнопки. Кормления soft-WDT (Linux
            // выключен) потребляем вхолостую.
            (void)wdt_handle_timed_out();
            watchdog_reload();                      // E1: кормим IWDG перед сном
            mcu_stop_enter();                       // E2-E5 + восстановление
            if (mcu_stop_take_wut_tick()) {
                wbec_ctx.suspend_wut_ticks++;       // W7: тик дедлайна
            }
            break;
        }

#if defined(WBEC_HAS_WARM_RESET)
        // Если Linux сбрасывает watchdog - система жива,
        // разрешаем следующую попытку тёплого сброса.
        // Это же событие завершает режим suspend: после пробуждения
        // Linux первым делом снова начинает кормить watchdog.
        if (wdt_handle_fed()) {
            wbec_ctx.wd_warm_reset_attempted = false;
            // Кормление завершает режим suspend только после того, как
            // сон реально начался (3.3В пропадало): до заморозки системы
            // демон watchdog продолжает кормить.
            if (wbec_ctx.suspend_mode && wbec_ctx.suspend_started) {
                wbec_ctx.suspend_mode = false;
                wbec_ctx.suspend_started = false;
                console_print_w_prefix("Suspend mode: off (watchdog fed)\r\n");
            }
        }

        if (wbec_ctx.suspend_mode) {
            // Во время объявленного сна watchdog-таймауты игнорируются
            // (Linux заморожен и не кормит намеренно), но запрошенная
            // длительность сна + запас служит дедлайном: если система
            // не проснулась - обычное восстановление тёплым сбросом.
            (void)wdt_handle_timed_out();
            if (systick_get_time_since_timestamp(wbec_ctx.suspend_entry_timestamp) >
                wbec_ctx.suspend_timeout_ms) {
                wbec_ctx.suspend_mode = false;
                if (!wbec_ctx.suspend_started) {
                    // Сон так и не начался (suspend отменён или не
                    // дошёл до отключения питания) - система живёт,
                    // просто тихо выходим из режима
                    console_print_w_prefix("Suspend mode: off (never started)\r\n");
                    break;
                }
                wbec_ctx.suspend_started = false;
                wbec_ctx.wd_warm_reset_attempted = true;
                wbec_ctx.poweron_reason = REASON_WATCHDOG_WARM;
                console_print("\r\n\n");
                console_print_w_prefix("Suspend deadline passed, system did not wake up - warm reset.\r\n");
                linux_cpu_pwr_seq_warm_reset();
                new_state(WBEC_STATE_POWER_ON_SEQUENCE_WAIT);
                break;
            }
        } else
        // Если сработал WDT - сначала пробуем тёплый сброс (DRAM сохраняется,
        // логи ramoops можно будет прочитать после перезагрузки).
        // Если после тёплого сброса система не ожила (Linux не сбросил
        // watchdog до следующего таймаута) - жёсткий сброс по питанию
        if (wdt_handle_timed_out()) {
            console_print("\r\n\n");
            if (!wbec_ctx.wd_warm_reset_attempted) {
                wbec_ctx.wd_warm_reset_attempted = true;
                wbec_ctx.poweron_reason = REASON_WATCHDOG_WARM;
                console_print_w_prefix("Watchdog is timed out, try warm reset first (DRAM is preserved).\r\n");
                linux_cpu_pwr_seq_warm_reset();
            } else {
                wbec_ctx.poweron_reason = REASON_WATCHDOG;
                console_print_w_prefix("Watchdog is timed out again, reset power.\r\n");
                linux_cpu_pwr_seq_hard_reset();
                // Жёсткий сброс - последняя ступень эскалации. После него
                // начинается новый цикл загрузки, и первое зависание в нём
                // снова заслуживает тёплого сброса (с сохранением DRAM/ramoops),
                // поэтому эскалация начинается заново
                wbec_ctx.wd_warm_reset_attempted = false;
            }
            new_state(WBEC_STATE_POWER_ON_SEQUENCE_WAIT);
        }
#else
        // Завершение режима suspend по первому кормлению watchdog -
        // только после реального начала сна (3.3В пропадало)
        if (wdt_handle_fed() && wbec_ctx.suspend_mode && wbec_ctx.suspend_started) {
            wbec_ctx.suspend_mode = false;
            wbec_ctx.suspend_started = false;
            console_print_w_prefix("Suspend mode: off (watchdog fed)\r\n");
        }

        if (wbec_ctx.suspend_mode) {
            // Во время объявленного сна watchdog-таймауты игнорируются;
            // дедлайн - запрошенная длительность сна + запас.
            (void)wdt_handle_timed_out();
            if (systick_get_time_since_timestamp(wbec_ctx.suspend_entry_timestamp) >
                wbec_ctx.suspend_timeout_ms) {
                wbec_ctx.suspend_mode = false;
                if (!wbec_ctx.suspend_started) {
                    console_print_w_prefix("Suspend mode: off (never started)\r\n");
                    break;
                }
                wbec_ctx.suspend_started = false;
                wbec_ctx.poweron_reason = REASON_WATCHDOG;
                console_print("\r\n\n");
                console_print_w_prefix("Suspend deadline passed, system did not wake up - reset power.\r\n");
                linux_cpu_pwr_seq_hard_reset();
                new_state(WBEC_STATE_POWER_ON_SEQUENCE_WAIT);
                break;
            }
        } else
        // Если сработал WDT - перезагружаемся по питанию
        if (wdt_handle_timed_out()) {
            wbec_ctx.poweron_reason = REASON_WATCHDOG;
            console_print("\r\n\n");
            console_print_w_prefix("Watchdog is timed out, reset power.\r\n");
            linux_cpu_pwr_seq_hard_reset();
            new_state(WBEC_STATE_POWER_ON_SEQUENCE_WAIT);
        }
#endif

        // Сработал watchdog - сброс уже начат. Контроль 3.3В в этом проходе
        // пропускаем, чтобы не запустить вторую последовательность поверх
        // начатой (см. комментарий выше)
        if (wbec_ctx.state != WBEC_STATE_WORKING) {
            break;
        }

        // Если пропало 3.3В - пробуем перезапустить питание, но не более N раз за M минут
        // Если питание пропадает слишком часто - выключаемся
        // Это происходит, например, при питании через плохой USB кабель.
        // В результате PMIC выключается, но питание на линии 5В остаётся.
        // Ограничение по числу попыток нужно, чтобы избежать циклического перезапуска.
        // В режиме suspend потеря 3.3В ожидаема: BL31 отключает DCDC1
        // на время сна - проверку пропускаем, но запоминаем факт
        // пропадания: с этого момента сон действительно начался.
        if (wbec_ctx.suspend_mode && !vmon_get_ch_status(VMON_CHANNEL_V33)) {
            wbec_ctx.suspend_started = true;
        }
        if (!wbec_ctx.suspend_mode && !vmon_get_ch_status(VMON_CHANNEL_V33)) {
            console_print_w_prefix("3.3V is lost\r\n");

            if (!vmon_get_ch_status(VMON_CHANNEL_V50)) {
                // Если при этом нет напряжения на линии 5В - это означает, что выдернули питание
                // и не надо пытаться включиться заново
                linux_cpu_pwr_seq_hard_off();
                new_state(WBEC_STATE_POWER_OFF_SEQUENCE_WAIT);
            } else if ((wbmz_is_powered_from_wbmz()) && (!wbmz_is_vbat_ok())) {
                // Может быть ситуация, особенно если нагрузка высокая, когда при полном разряде АКБ +5В
                // пропадает кратковременно (на 5 мс) и это не детектируется.
                // При этом +3.3В пропадает, нагрузка снимается, Vbat растет, WB начинает перезагружаться и
                // так несколько раз, пока WBMZ не разрядится окончательно.
                // Поэтому, надо проверить, какое напряжение было на WBMZ в момент пропадания +3.3В.
                // Если оно слишком низкое - значит надо выключить WBMZ. Далее всё выключиться само,
                // если после отключения WBMZ пропадет +5В (нет внешнего питания или USB).
                // А если есть внешнее питание - то продолжит работать от него.
                console_print_w_prefix("WBMZ battery is fully discharged, disable WBMZ to prevent reboot loop\r\n");
                wbmz_disable_stepup();
                linux_cpu_pwr_seq_hard_reset();
                new_state(WBEC_STATE_POWER_ON_SEQUENCE_WAIT);
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
                    console_print_w_prefix("Try to reset power\r\n");
                    console_print_w_prefix("Enable WBMZ to prevent power loss under load\r\n");
                    wbec_ctx.poweron_reason = REASON_PMIC_OFF;
                    wbmz_enable_stepup();
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

#ifdef __unittest_env__
    void utest_wbec_reset_state(void)
    {
        memset(&wbec_ctx, 0, sizeof(wbec_ctx));
    }

    // «Сон начался» / разрешение выхода из suspend по кормлению watchdog.
    // Должен возвращаться в исходное (false) состояние после выхода из режима.
    bool utest_wbec_get_suspend_started(void)
    {
        return wbec_ctx.suspend_started;
    }
#endif
