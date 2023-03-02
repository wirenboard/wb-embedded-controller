#pragma once
#include <stdint.h>
#include <stdbool.h>

enum regmap_rw {
    REGMAP_RO,
    REGMAP_RW
};

#define REGMAP(m) \
    /* INFO: id and fw version */ \
    m( \
        INFO,       /* Region name */ \
        0x00,       /* Region base address */ \
        REGMAP_RO,  /* RO/RW */ \
        /* Region data */ \
        uint8_t wbec_id; \
        uint8_t board_rev; \
        uint8_t fw_ver_major; \
        uint8_t fw_ver_minor; \
        uint8_t fw_ver_patch; \
        int8_t fw_ver_suffix; \
    ) \
    /* RTC Time */ \
    m( \
        RTC_TIME,   /* Region name */ \
        0x10,       /* Region base address */ \
        REGMAP_RW,  /* RO/RW */ \
        /* Region data */ \
        uint8_t seconds; \
        uint8_t minutes; \
        uint8_t hours; \
        uint8_t days; \
        uint8_t weekdays; \
        uint8_t months; \
        uint8_t years; \
    ) \
    /* RTC Alarm */ \
    m( \
        RTC_ALARM,  /* Region name */ \
        0x20,       /* Region base address */ \
        REGMAP_RW,  /* RO/RW */ \
        /* Region data */ \
        uint8_t seconds; \
        uint8_t minutes; \
        uint8_t hours; \
        uint8_t days; \
        bool en:1; \
    ) \
    /* RTC Config */ \
    m( \
        RTC_CFG,    /* Region name */ \
        0x30,       /* Region base address */ \
        REGMAP_RW,  /* RO/RW */ \
        /* Region data */ \
        uint8_t offset; \
    ) \
    /* ADC Data: voltages, temperature */ \
    m( \
        ADC_DATA,   /* Region name */ \
        0x40,       /* Region base address */ \
        REGMAP_RW,  /* RO/RW */ \
        /* Region data */ \
        uint16_t v_in; \
        uint16_t v_bat; \
        uint16_t v_3_3; \
        uint16_t v_5_0; \
        uint16_t v_a1; \
        uint16_t v_a2; \
        uint16_t v_a3; \
        uint16_t v_a4; \
        uint16_t temp; \
        uint16_t v_usb_debug_console; \
        uint16_t v_usb_debug_network; \
    ) \
    /* ADC Config: UVP / OVP */ \
    m( \
        ADC_CFG,   /* Region name */ \
        0x60,       /* Region base address */ \
        REGMAP_RW,  /* RO/RW */ \
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
    /* GPIO */ \
    m( \
        GPIO,       /* Region name */ \
        0x80,       /* Region base address */ \
        REGMAP_RW,  /* RO/RW */ \
        /* Region data */ \
        bool a1:1; \
        bool a2:1; \
        bool a3:1; \
        bool a4:1; \
        bool v_out:1; \
    ) \
    /* Watchdog */ \
    m( \
        WDT,        /* Region name */ \
        0x90,       /* Region base address */ \
        REGMAP_RW,  /* RO/RW */ \
        /* Region data */ \
        uint8_t timeout:4; \
        bool reset:1; \
        bool run:1; \
    ) \
    /* Power control */ \
    m( \
        POWER_CTRL, /* Region name */ \
        0xA0,       /* Region base address */ \
        REGMAP_RW,  /* RO/RW */ \
        /* Region data */ \
        bool off:1; \
    ) \
    /* IRQ Flags */ \
    m( \
        IRQ_FLAGS,        /* Region name */ \
        0xB0,       /* Region base address */ \
        REGMAP_RO,  /* RO/RW */ \
        /* Region data */ \
        uint8_t irqs; \
    ) \
    /* IRQ Mask */ \
    m( \
        IRQ_MSK,    /* Region name */ \
        0xB2,       /* Region base address */ \
        REGMAP_RW,  /* RO/RW */ \
        /* Region data */ \
        uint8_t irqs; \
    ) \
    /* IRQ Clear */ \
    m( \
        IRQ_CLEAR,        /* Region name */ \
        0xB4,       /* Region base address */ \
        REGMAP_RW,  /* RO/RW */ \
        /* Region data */ \
        uint8_t irqs; \
    ) \


#define __REGMAP_STRUCTS(name, addr, rw, members)       struct __attribute__((packed)) REGMAP_##name { members };

REGMAP(__REGMAP_STRUCTS)
