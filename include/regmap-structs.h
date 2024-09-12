#pragma once
#include <stdint.h>

#define REGMAP(m) \
    /*     Addr     Name            RO/RW */ \
    m(     0x00,    HW_INFO_PART1,  RO, \
        /* 0x00 */  uint16_t wbec_id; \
        /* 0x01 */  uint16_t hwrev_code : 12; \
        /* 0x01 */  uint16_t hwrev_error_flag : 4; \
                    union { \
                        struct { \
        /* 0x02 */          uint16_t fwrev_major; \
        /* 0x03 */          uint16_t fwrev_minor; \
        /* 0x04 */          uint16_t fwrev_patch; \
        /* 0x05 */          int16_t fwrev_suffix; \
                        }; \
                        uint16_t fwrev[4]; \
                    }; \
    ) \
    /*     Addr     Name            RO/RW */ \
    m(     0x06,    POWERON_REASON, RO, \
        /* 0x06 */  uint16_t poweron_reason; \
    ) \
    /*     Addr     Name            RO/RW */ \
    m(     0x07,    HW_INFO_PART2,  RO, \
        /* 0x07-0x0C */ uint16_t uid[6]; \
        /* 0x0D */  uint16_t hwrev_ok; \
    ) \
    /*     Addr     Name            RO/RW */ \
    m(     0x10,    RTC_TIME,       RW, \
        /* 0x10 */  uint16_t seconds : 8; \
        /* -//- */  uint16_t minutes : 8; \
        /* 0x11 */  uint16_t hours : 8; \
        /* -//- */  uint16_t days : 8; \
        /* 0x12 */  uint16_t weekdays : 8; \
        /* -//- */  uint16_t months : 8; \
        /* 0x13 */  uint16_t years; \
    ) \
    /*     Addr     Name            RO/RW */ \
    m(     0x20,    RTC_ALARM,      RW, \
        /* 0x20 */  uint16_t seconds : 8; \
        /* -//- */  uint16_t minutes : 8; \
        /* 0x21 */  uint16_t hours : 8; \
        /* -//- */  uint16_t days : 8; \
        /* 0x22 */  uint16_t en : 1; \
    ) \
    /*     Addr     Name            RO/RW */ \
    m(     0x30,    RTC_CFG,        RW, \
        /* 0x30 */  uint16_t offset; \
    ) \
    /*     Addr     Name            RO/RW */ \
    m(     0x40,    ADC_DATA,       RO, \
        /* 0x40 */  uint16_t v_in; \
        /* 0x41 */  uint16_t v_3_3; \
        /* 0x42 */  uint16_t v_5_0; \
        /* 0x43 */  uint16_t vbus_console; \
        /* 0x44 */  uint16_t vbus_network; \
        /* 0x45 */  int16_t temp; \
        /* 0x46 */  uint16_t v_a1; \
        /* 0x47 */  uint16_t v_a2; \
        /* 0x48 */  uint16_t v_a3; \
        /* 0x49 */  uint16_t v_a4; \
    ) \
    /*     Addr     Name            RO/RW */ \
    m(     0x80,    GPIO,           RW, \
        /* 0x80 */  uint16_t a1 : 1; \
        /* -//- */  uint16_t a2 : 1; \
        /* -//- */  uint16_t a3 : 1; \
        /* -//- */  uint16_t a4 : 1; \
        /* -//- */  uint16_t v_out : 1; \
    ) \
    /*     Addr     Name            RO/RW */ \
    m(     0x90,    WDT,            RW, \
        /* 0x90 */  uint16_t timeout; \
        /* 0x91 */  uint16_t reset : 1; \
    ) \
    /*     Addr     Name            RO/RW */ \
    m(     0xA0,    POWER_CTRL,     RW, \
        /* 0xA0 */  uint16_t off : 1; \
        /* -//- */  uint16_t reboot : 1; \
        /* -//- */  uint16_t reset_pmic : 1; \
    ) \
    /*     Addr     Name            RO/RW */ \
    m(     0xB0,    IRQ_FLAGS,      RO, \
        /* 0xB0 */  uint16_t irqs; \
    ) \
    /*     Addr     Name            RO/RW */ \
    m(     0xB2,    IRQ_MSK,        RW, \
        /* 0xB2 */  uint16_t irqs; \
    ) \
    /*     Addr     Name            RO/RW */ \
    m(     0xB4,    IRQ_CLEAR,      RW, \
        /* 0xB4 */  uint16_t irqs; \
    ) \
    /*     Addr     Name            RO/RW */ \
    m(     0xC0,    PWR_STATUS,     RO, \
        /* 0xC0 */  uint16_t powered_from_wbmz : 1; \
        /* 0xC0 */  uint16_t wbmz_stepup_enabled : 1; \
        /* 0xC0 */  uint16_t wbmz_charging_enabled : 1; \
        /* 0xC0 */  uint16_t wbmz_battery_present : 1; \
        /* 0xC0 */  uint16_t wbmz_supercap_present : 1; \
        /* 0xC0 */  uint16_t wbmz_is_charging : 1; \
        /* 0xC0 */  uint16_t wbmz_is_dead : 1; \
        /* 0xC0 */  uint16_t wbmz_is_inserted : 1; \
        /* 0xC1 */  uint16_t wbmz_full_design_capacity; \
        /* 0xC2 */  uint16_t wbmz_voltage_min_design; \
        /* 0xC3 */  uint16_t wbmz_voltage_max_design; \
        /* 0xC4 */  uint16_t wbmz_constant_charge_current; \
        /* 0xC5 */  uint16_t wbmz_battery_voltage; \
        /* 0xC6 */  uint16_t wbmz_charging_current; \
        /* 0xC7 */  uint16_t wbmz_discharging_current; \
        /* 0xC8 */  uint16_t wbmz_capacity_percent; \
        /* 0xC9 */  int16_t wbmz_temperature; \
    ) \
    /*     Addr     Name            RO/RW */ \
    m(     0xD0,    BUZZER_CTRL,    RW, \
        /* 0xD0 */  uint16_t freq_hz; \
        /* 0xD1 */  uint16_t duty_percent; \
        /* 0xD2 */  uint16_t enabled : 1; \
    ) \
    /*     Addr     Name            RO/RW */ \
    m(     0xF0,    TEST,           RW, \
        /* 0xF0 */  uint16_t send_test_message : 1; \
        /* 0xF0 */  uint16_t enable_rtc_out : 1; \
        /* 0xF0 */  uint16_t reset_rtc : 1; \
        /* 0xF0 */  uint16_t heater_force_enable : 1; \
    ) \

// Общее число регистров в адресном пространстве
// Число должно быть больше или равно адресу последнего регистра
// Должно быть степенью двойки
// По этому числу происходит циклической автоинкремент адреса
#define REGMAP_TOTAL_REGS_COUNT         256

#define __REGMAP_STRUCTS(addr, name, rw, members)       struct __attribute__((packed)) REGMAP_##name { members };

REGMAP(__REGMAP_STRUCTS)
