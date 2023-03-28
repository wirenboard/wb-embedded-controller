#include "config.h"
#include "regmap-int.h"
#include "pwrkey.h"
#include "irq-subsystem.h"
#include "wdt.h"
#include "wb-power.h"
#include "system-led.h"
#include "ntc.h"
#include "adc.h"
#include "usart_tx.h"
#include "systick.h"
#include "wbmcu_system.h"
#include "array_size.h"

#define WBEC_STARTUP_TIMEOUT_MS                     20

static const char fwver_chars[] = { MODBUS_DEVICE_FW_VERSION_STRING };

// Причина включения питания Linux
enum poweron_reason {
    REASON_POWER_ON,        // Подано питание на WB
    REASON_POWER_KEY,       // Нажата кнопка питания (до этого было выключено)
    REASON_RTC_ALARM,       // Будильник
    REASON_REBOOT,          // Перезагрузка
    REASON_WATHDOG,         // Сработал watchdog
    REASON_UNKNOWN,         // Неизветсно что (на всякий случай)
};

static const char * power_reason_strings[] = {
    "Wiren Board supply on",
    "Power key pressed",
    "RTC alarm",
    "Reboot",
    "Watchdog",
    "Unknown",
};

// Состояние алгоритма EC
enum wbec_state {
    WBEC_STATE_WAIT_STARTUP,
    WBEC_STATE_VOLTAGE_CHECK,
    WBEC_STATE_WORKING,
    WBEC_POWERING_OFF,
    WBEC_REBOOTING,
    WBEC_WAIT_STANDBY,
};

struct wbec_ctx {
    enum poweron_reason poweron_reason;
    enum wbec_state state;
    systime_t timestamp;
};

static struct REGMAP_INFO wbec_info = {
    .wbec_id = WBEC_ID,
    MODBUS_DEVICE_FW_VERSION_NUMBERS,
};

static struct wbec_ctx wbec_ctx;

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

static inline void collect_adc_data(struct REGMAP_ADC_DATA adc)
{
    // Get voltages
    adc.v_a1 = adc_get_ch_mv(ADC_CHANNEL_ADC_IN1);
    adc.v_a2 = adc_get_ch_mv(ADC_CHANNEL_ADC_IN2);
    adc.v_a3 = adc_get_ch_mv(ADC_CHANNEL_ADC_IN3);
    adc.v_a4 = adc_get_ch_mv(ADC_CHANNEL_ADC_IN4);
    adc.v_in = adc_get_ch_mv(ADC_CHANNEL_ADC_V_IN);
    adc.v_5_0 = adc_get_ch_mv(ADC_CHANNEL_ADC_5V);
    adc.v_3_3 = adc_get_ch_mv(ADC_CHANNEL_ADC_3V3);
    adc.vbus_console = adc_get_ch_mv(ADC_CHANNEL_ADC_VBUS_DEBUG);
    adc.vbus_network = adc_get_ch_mv(ADC_CHANNEL_ADC_VBUS_NETWORK);

    // Calc NTC temp
    fix16_t ntc_raw = adc_get_ch_mv(ADC_CHANNEL_ADC_NTC);
    // Convert to x10 *C
    adc.temp = fix16_to_int(
        fix16_mul(
            ntc_get_temp(ntc_raw),
            F16(100)
        )
    );
}

void wbec_init(void)
{
    wbec_info.poweron_reason = get_poweron_reason();

    regmap_set_region_data(REGMAP_REGION_INFO, &wbec_info, sizeof(wbec_info));

    wdt_set_timeout(WDEC_WATCHDOG_INITIAL_TIMEOUT_S);

    wbec_ctx.timestamp = 0;
}

void wbec_do_periodic_work(void)
{
    struct REGMAP_ADC_DATA adc;
    collect_adc_data(&adc);
    regmap_set_region_data(REGMAP_REGION_ADC_DATA, &adc, sizeof(adc));


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
        if ((wbec_ctx.poweron_reason == REASON_POWER_ON) && (adc.v_5_0 > 4500)) {
            // Если после включени МК +5В есть - значить линукс уже работает
            // Не нужно вывоодить информацию в уарт
            // Тут ничего не нужно делать
        } else {
            // В ином случае перехватываем питание (выключаем)
            // И отправляем инофрмацию в уарт
            wb_power_off();

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

            // TODO? usart_tx_strn_blocking("Voltages: ");

            usart_tx_strn_blocking("Power on now...\n\n");
        }

        wb_power_on();
        wbec_ctx.state = WBEC_STATE_WORKING;
        break;

    case WBEC_STATE_WORKING:
        // В этом состоянии линукс работает
        // И всё остальное тоже работает



        break;

    }



    // Power off request from button
    if (pwrkey_handle_short_press()) {
        wdt_stop();
        irq_set_flag(IRQ_PWR_OFF_REQ);
        wb_power_off_and_sleep(WBEC_LINUX_POWER_OFF_DELAY_MS);
        system_led_blink(250, 250);
    }

    // Immediately power off from button
    if (pwrkey_handle_long_press()) {
        wb_power_off_and_sleep(0);
    }

    // Linux is ready to power off
    if (regmap_is_region_changed(REGMAP_REGION_POWER_CTRL)) {
        struct REGMAP_POWER_CTRL p;
        regmap_get_region_data(REGMAP_REGION_POWER_CTRL, &p, sizeof(p));
        if (p.off) {
            wb_power_off_and_sleep(0);
        }
        if (p.reboot) {
            wdt_stop();
            system_led_blink(50, 50);
            wb_power_reset();
        }

        regmap_clear_changed(REGMAP_REGION_POWER_CTRL);
    }

    // Check watchdog timed out
    if (wdt_handle_timed_out()) {
        system_led_blink(50, 50);
        wb_power_reset();
    }
}

