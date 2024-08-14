#include "config.h"
#include "wbmz-common.h"
#include "adc.h"
#include "voltage-monitor.h"
#include "gpio.h"
#include "systick.h"

static const gpio_pin_t wbmz_stepup_enable_gpio = { EC_GPIO_WBMZ_STEPUP_ENABLE };
static const gpio_pin_t wbmz_status_bat_gpio = { EC_GPIO_WBMZ_STATUS_BAT };

static bool stepup_enabled;

static void wbmz_stepup_control(void)
{
    bool usb = vmon_get_ch_status(VMON_CHANNEL_VBUS_DEBUG);
    #if !defined(EC_USB_HUB_DEBUG_NETWORK)
        usb = usb || vmon_get_ch_status(VMON_CHANNEL_VBUS_NETWORK);
    #endif
    bool vin = vmon_get_ch_status(VMON_CHANNEL_V_IN_FOR_WBMZ);
    static systime_t wbmz_disable_filter_ms;

    // Управление повышающим преобразователем в WBMZ
    if (stepup_enabled) {
        // Если stepup в WBMZ работает - работаем от батареи до тех пор, пока она не разрядится
        if ((!vin) && (!wbmz_is_powered_from_wbmz())) {
            // Факт разряда батареи - совпадение условий
            // - WBMZ включен
            // - нет достаточного (11.5В) напряжения на Vin
            // - нет сигнала STATUS_BAT
            // Нужно выключить WBMZ для того, чтобы не уходить в цикл заряда-разряда
            // Если 5В при этом пропадет - перейдем в standby
            // Если останется - продолжим работать
            if (systick_get_time_since_timestamp(wbmz_disable_filter_ms) > 500) {
                // Этот фильтр нужен, т.к. сигналы имеют разную природу
                // STATUS_BAT - это GPIO, остальное - АЦП.
                // Нужно время, чтобы убедиться что они все возникли одновременно
                // и исключить переходные процессы
                wbmz_disable_stepup();
            }
        } else {
            wbmz_disable_filter_ms = systick_get_system_time_ms();
        }
    } else {
        // Если Vin превысило порог включения WBMZ - включим его
        // Но только если нет USB, т.к. в этом случае WBMZ будет всегда разряжаться
        // Требование такое: если работаем от USB, не нужно включать батарейку
        // Однако если хотим чтоб WBMZ работал при подключенном USB, надо
        // сначала включить контроллер кнопкой, а потом подключать USB
        if (vin && (!usb)) {
            wbmz_enable_stepup();
        }
    }
}

void wbmz_init(void)
{
    wbmz_disable_stepup();
    GPIO_S_SET_OUTPUT(wbmz_stepup_enable_gpio);

    // STATUS_BAT это вход, который WBMZ тянет к земле открытым коллектором
    // Подтянут снаружи к V_EC
    GPIO_S_SET_INPUT(wbmz_status_bat_gpio);
}

void wbmz_do_periodic_work(void)
{
    wbmz_stepup_control();
}

void wbmz_enable_stepup(void)
{
    // Вызывается только один раз либо в wbec_init, либо в linux_cpu_pwr_seq_do_periodic_work
    // Однажны включившийся WBMZ более не выключается
    GPIO_S_SET(wbmz_stepup_enable_gpio);
    stepup_enabled = true;
}

void wbmz_disable_stepup(void)
{
    GPIO_S_RESET(wbmz_stepup_enable_gpio);
    stepup_enabled = false;
}

bool wbmz_is_powered_from_wbmz(void)
{
    if (GPIO_S_TEST(wbmz_status_bat_gpio)) {
        return false;
    }
    return true;
}

bool wbmz_is_stepup_enabled(void)
{
    return stepup_enabled;
}
