#pragma once
#include <stdint.h>

#define REGMAP(m) \
    /*  Name        Addr    RO/RW */ \
    m(  INFO,       0x00,   RO, \
        /* Region data */ \
        uint16_t wbec_id; \
        uint16_t board_rev; \
        uint8_t fw_ver_major; \
        uint8_t fw_ver_minor; \
        uint8_t fw_ver_patch; \
        int8_t fw_ver_suffix; \
    ) \
    /*  Name        Addr    RO/RW */ \
    m(  RTC_TIME,   0x10,   RW, \
        /* Region data */ \
        uint8_t seconds; \
        uint8_t minutes; \
        uint8_t hours; \
        uint8_t days; \
        uint8_t weekdays; \
        uint8_t months; \
        uint16_t years; \
    ) \
    /*  Name        Addr    RO/RW */ \
    m(  RTC_ALARM,  0x20,   RW, \
        /* Region data */ \
        uint8_t seconds; \
        uint8_t minutes; \
        uint8_t hours; \
        uint8_t days; \
        uint16_t en:1; \
    ) \
    /*  Name        Addr    RO/RW */ \
    m(  RTC_CFG,    0x30,   RW, \
        /* Region data */ \
        uint16_t offset; \
    ) \
    /*  Name        Addr    RO/RW */ \
    m(  ADC_DATA,   0x40,   RW, \
        /* Region data */ \
        uint16_t v_in; \
        uint16_t v_3_3; \
        uint16_t v_5_0; \
        uint16_t v_a1; \
        uint16_t v_a2; \
        uint16_t v_a3; \
        uint16_t v_a4; \
        uint16_t temp; \
        uint16_t vbus_console; \
        uint16_t vbus_network; \
    ) \
    /*  Name        Addr    RO/RW */ \
    m(  ADC_CFG,    0x60,   RW, \
        /* Region data */ \
        uint8_t lowpass_rc_a1; \
        uint8_t lowpass_rc_a2; \
        uint8_t lowpass_rc_a3; \
        uint8_t lowpass_rc_a4; \
        uint8_t v_in_uvp; \
        uint8_t v_in_ovp; \
        uint8_t v_out_uvp; \
        uint8_t v_out_ovp; \
    ) \
    /*  Name        Addr    RO/RW */ \
    m(  GPIO,       0x80,   RW, \
        /* Region data */ \
        uint16_t a1:1; \
        uint16_t a2:1; \
        uint16_t a3:1; \
        uint16_t a4:1; \
        uint16_t v_out:1; \
    ) \
    /*  Name        Addr    RO/RW */ \
    m(  WDT,        0x90,   RW, \
        /* Region data */ \
        uint16_t timeout; \
        uint16_t reset:1; \
        uint16_t run:1; \
    ) \
    /*  Name        Addr    RO/RW */ \
    m(  POWER_CTRL, 0xA0,   RW, \
        /* Region data */ \
        uint16_t off:1; \
        uint16_t reboot:1; \
    ) \
    /*  Name        Addr    RO/RW */ \
    m(  IRQ_FLAGS,  0xB0,   RO, \
        /* Region data */ \
        uint16_t irqs; \
    ) \
    /*  Name        Addr    RO/RW */ \
    m(  IRQ_MSK,    0xB2,   RW, \
        /* Region data */ \
        uint16_t irqs; \
    ) \
    /*  Name        Addr    RO/RW */ \
    m(  IRQ_CLEAR,  0xB4,   RW, \
        /* Region data */ \
        uint16_t irqs; \
    ) \


#define __REGMAP_STRUCTS(name, addr, rw, members)       struct __attribute__((packed)) REGMAP_##name { members };

REGMAP(__REGMAP_STRUCTS)
