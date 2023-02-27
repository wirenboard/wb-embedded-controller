#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "irq.h"

#define __REGMAP_STRUCT         struct __attribute__((packed))

#define REGMAP(m) \
    /* Type                         Name            RW */ \
    m(struct regmap_info,           INFO,           0   ) \
    m(struct regmap_rtc_time,       RTC_TIME,       1   ) \
    m(struct regmap_rtc_alarm,      RTC_ALARM,      1   ) \
    m(struct regmap_rtc_cfg,        RTC_CFG,        1   ) \
    m(struct regmap_adc_data,       ADC_DATA,       0   ) \
    m(struct regmap_adc_cfg,        ADC_CFG,        1   ) \
    m(struct regmap_gpio,           GPIO,           1   ) \
    m(struct regmap_watchdog,       WDT,            1   ) \
    m(struct regmap_power_control,  POWER_CTRL,     1   ) \
    m(irq_flags_t,                  IRQ_FLAGS,      0   ) \
    m(irq_flags_t,                  IRQ_MSK,        1   ) \
    m(irq_flags_t,                  IRQ_CLEAR,      1   ) \

__REGMAP_STRUCT regmap_info {
    uint8_t wbec_id;
    uint8_t board_rev;
    uint8_t fw_ver_major;
    uint8_t fw_ver_minor;
    uint8_t fw_ver_patch;
    int8_t fw_ver_suffix;
};

__REGMAP_STRUCT regmap_rtc_time {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t days;
    uint8_t weekdays;
    uint8_t months;
    uint8_t years;
};

__REGMAP_STRUCT regmap_rtc_alarm {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t days;
    bool en:1;
    bool flag:1;
};

__REGMAP_STRUCT regmap_rtc_cfg {
    uint8_t offset;
    uint8_t res;
};

__REGMAP_STRUCT regmap_adc_data {
    uint16_t v_in;
    uint16_t v_bat;
    uint16_t v_3_3;
    uint16_t v_5_0;
    uint16_t v_a1;
    uint16_t v_a2;
    uint16_t v_a3;
    uint16_t v_a4;
    uint16_t temp;
    uint16_t v_usb_debug_console;
    uint16_t v_usb_debug_network;
};

__REGMAP_STRUCT regmap_adc_cfg {
    uint8_t lowpass_rc_a1;
    uint8_t lowpass_rc_a2;
    uint8_t lowpass_rc_a3;
    uint8_t lowpass_rc_a4;
    uint8_t v_in_uvp;
    uint8_t v_in_ovp;
    uint8_t v_out_uvp;
    uint8_t v_out_ovp;
};

__REGMAP_STRUCT regmap_gpio {
    bool a1:1;
    bool a2:1;
    bool a3:1;
    bool a4:1;
    bool v_out:1;
};

__REGMAP_STRUCT regmap_watchdog {
    uint8_t timeout:4;
    bool reset:1;
    bool run:1;
};

__REGMAP_STRUCT regmap_power_control {
    bool off:1;
    bool pwrkey_pressed:1;
};
