#include "hwrev.h"
#include "config.h"
#include "adc.h"
#include "fix16.h"
#include "array_size.h"
#include "regmap-structs.h"
#include "regmap-int.h"
#include "wbmcu_system.h"
#include <string.h>
#include "mcu-pwr.h"
#include "spi-slave.h"
#include "systick.h"
#include "system-led.h"
#include "rcc.h"
#include "wdt-stm32.h"

#define HWREV_ADC_VALUE_EXPECTED(res_up, res_down) \
    ((res_down) * 4096 / ((res_up) + (res_down)))

#define HWREV_ADC_VALUE_MIN(res_up, res_down) \
    HWREV_ADC_VALUE_EXPECTED(res_up, res_down) - \
    (HWREV_ADC_VALUE_EXPECTED(res_up, res_down) * WBEC_HWREV_DIFF_PERCENT / 100) - \
    WBEC_HWREV_DIFF_ADC

#define HWREV_ADC_VALUE_MAX(res_up, res_down) \
    HWREV_ADC_VALUE_EXPECTED(res_up, res_down) + \
    (HWREV_ADC_VALUE_EXPECTED(res_up, res_down) * WBEC_HWREV_DIFF_PERCENT / 100) + \
    WBEC_HWREV_DIFF_ADC

#define __HWREV_DATA(hwrev_name, hwrev_code, res_up, res_down) \
    { \
        .code = hwrev_code, \
        .adc_min = HWREV_ADC_VALUE_MIN(res_up, res_down), \
        .adc_max = HWREV_ADC_VALUE_MAX(res_up, res_down), \
    },

struct hwrev_desc {
    uint16_t code;
    int16_t adc_min;
    int16_t adc_max;
};

static const struct hwrev_desc hwrev_desc[HWREV_COUNT] = {
    WBEC_HWREV_DESC(__HWREV_DATA)
};

static enum hwrev hwrev = HWREV_UNKNOWN;
static uint16_t hwrev_code = HWREV_UNKNOWN;

static void hwrev_init(void)
{
    #if defined DEBUG
        // При отладке через SWD мы не можем понять какая аппаратная ревизия
        // поэтому считаем, что это WBEC_HWREV (всегда правильная ревизия)
        hwrev = WBEC_HWREV;
        hwrev_code = hwrev_desc[WBEC_HWREV].code;
        return;
    #endif

    int16_t hwrev_adc_value = fix16_to_int(adc_get_ch_adc_raw(ADC_CHANNEL_ADC_HW_VER));

    for (int i = 0; i < HWREV_COUNT; i++) {
        if (hwrev_adc_value >= hwrev_desc[i].adc_min &&
            hwrev_adc_value <= hwrev_desc[i].adc_max) {
            hwrev = i;
            hwrev_code = hwrev_desc[i].code;
            break;
        }
    }
}

enum hwrev hwrev_get(void)
{
    return hwrev;
}

void hwrev_put_hw_info_to_regmap(void)
{
    struct REGMAP_HW_INFO_PART1 hw_info_1 = {
        .wbec_id = WBEC_ID,
        .hwrev_code = 0,
        .hwrev_error_flag = 0,
        .fwrev = { FW_VERSION_NUMBERS },
    };
    struct REGMAP_HW_INFO_PART2 hw_info_2 = {};

    hw_info_1.hwrev_code = hwrev_code;
    memcpy(hw_info_2.uid, (uint8_t *)UID_BASE, sizeof(hw_info_2.uid));

    if (hwrev == WBEC_HWREV) {
        hw_info_2.hwrev_ok = WBEC_ID;
    } else {
        hw_info_1.hwrev_error_flag = 0b1010;
    }

    regmap_set_region_data(REGMAP_REGION_HW_INFO_PART1, &hw_info_1, sizeof(hw_info_1));
    regmap_set_region_data(REGMAP_REGION_HW_INFO_PART2, &hw_info_2, sizeof(hw_info_2));
}

void hwrev_init_and_check(void)
{
    enum mcu_poweron_reason poweron_reason = mcu_get_poweron_reason();

    // hwrev имеет смысл проверять только при включении питания
    // Все остальные причины включения не могут возникнуть сами по себе,
    // это означает что прошивка уже была загружена и hwrev уже был проверен,
    // поэтому нет смысла проверять его ещё раз.
    if (poweron_reason != MCU_POWERON_REASON_POWER_ON) {
        // заполняем поля hwrev, не проверяя его
        hwrev = WBEC_HWREV;
        hwrev_code = hwrev_desc[WBEC_HWREV].code;
        hwrev_put_hw_info_to_regmap();
        return;
    }

    // Прежде чем инициализировать всё остальное, нужно проверить совместимость железа и прошивки
    hwrev_init();
    if (hwrev_get() != WBEC_HWREV) {
        // Прошивка несовместима с железом
        // Дальше что-то делать смысла нет, т.к. мы не знаем что за железо и какие gpio чем управляют
        // Инициализируем только системный светодиод, spi и regmap
        // Чтобы из линукса можно было прочитать код железа и понять какую прошивку заливать
        rcc_set_hsi_pll_64mhz_clock();
        systick_init();
        spi_slave_init();
        regmap_init();
        hwrev_put_hw_info_to_regmap();
        // Редко мигаем светодиодом, чтобы понять что прошивка несовместима
        system_led_blink(25, 25);
        while (1) {
            system_led_do_periodic_work();
            // На всякий случай перезагружаем микроконтроллер каждые 10 секунд
            // для повторной проверки hwrev, если проверка оказалась ложно-отрицательной
            if (systick_get_system_time_ms() > 10000) {
                NVIC_SystemReset();
            }
            watchdog_reload();
        }
    }
}
