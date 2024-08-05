#include "config.h"

#if defined WBEC_WBMZ6_SUPPORT
#include "axp221s.h"
#include "adc.h"
#include "software_i2c.h"
#include <assert.h>
#include "array_size.h"
#include "wbmz6-battery.h"

static_assert(WBEC_WBMZ6_BATTERY_VOLTAGE_MIN_MV == 2900, "Only 2900 mV min voltage supported");
static_assert(WBEC_WBMZ6_BATTERY_VOLTAGE_MAX_MV == 4100, "Only 4100 mV max voltage supported");
static_assert(WBEC_WBMZ6_BATTERY_CHARGE_CURRENT_MA == 600, "Only 600 mA charge current supported");

#define AXP221S_ADDR                                0x68

#define AXP221S_REG_ADC_ENABLE                      0x82
#define AXP221S_REG_ADC_ADC_RATE                    0x84
#define AXP221S_REG_VOFF_SETTING                    0x31
#define AXP221S_REG_CHARGE_CONTROL_1                0x33
#define AXP221S_REG_TOTAL_BAT_CAPACITY_1            0xE0
#define AXP221S_REG_TOTAL_BAT_CAPACITY_2            0xE1

#define AXP221S_TS_PIN_ADC_DATA                     0x58
#define AXP221S_REG_BATTERY_VOLTAGE                 0x78
#define AXP221S_REG_BATTERY_CHARGE_CURRENT          0x7A
#define AXP221S_REG_BATTERY_LEVEL                   0xB9

static const uint8_t wbmz6_init_sequence[][2] = {
    {
        AXP221S_REG_ADC_ENABLE,
        // Battery voltage ADC enable
        // Battery current ADC enable
        // Internal temperature ADC enable
        // TS pin ADC enable
        0xE1
    },
    {
        AXP221S_REG_ADC_ADC_RATE,
        // 100Hz sampling rate
        // 80 uA TS current
        // TS pin function selection - Battery temperature monitoring function
        // TS pin current output mode - Input during ADC sampling, which can save power
        0x32
    },
    {
        AXP221S_REG_VOFF_SETTING,
        // Voff = 2.9V
        0x03
    },
    {
        AXP221S_REG_CHARGE_CONTROL_1,
        // Charge enable
        // 4.1V target voltage
        // Charge current 600mA
        // End charging when the charging current is less than 10% of the set value
        0x82
    }
};

enum wbmz6_device {
    WBMZ6_DEVICE_NONE,
    WBMZ6_DEVICE_BATTERY,
    WBMZ6_DEVICE_SUPERCAP,
};

static inline bool axp221s_is_present(void)
{
    if (software_i2c_detect_device(I2C_PORT_WBMZ6, AXP221S_ADDR) == I2C_STATUS_OK) {
        return true;
    }
    return false;
}

static inline bool axp221s_write_u8(uint8_t reg, uint8_t value)
{
    software_i2c_status_t ret;
    ret = software_i2c_write(I2C_PORT_WBMZ6, AXP221S_ADDR, &reg, 1, &value, 1);
    if (ret == I2C_STATUS_OK) {
        return true;
    }
    return false;
}

static inline bool axp221s_write_u16(uint8_t reg, uint16_t value)
{
    software_i2c_status_t ret;
    uint8_t buf[2];
    buf[0] = value & 0xFF;
    buf[1] = value >> 8;
    ret = software_i2c_write(I2C_PORT_WBMZ6, AXP221S_ADDR, &reg, 1, buf, 2);
    if (ret == I2C_STATUS_OK) {
        return true;
    }
    return false;
}

static inline uint16_t axp221s_read_u16(uint8_t reg)
{
    uint8_t buf[2];
    software_i2c_read_after_write(I2C_PORT_WBMZ6, AXP221S_ADDR, &reg, 1, buf, 2);
    return buf[0] | (buf[1] << 8);
}

static enum wbmz6_device wmbz6_detect_device(void)
{
    enum wbmz6_device ret = WBMZ6_DEVICE_NONE;

    if (axp221s_is_present()) {
        ret = WBMZ6_DEVICE_BATTERY;
    } else {
        uint16_t supercap_mv = adc_get_ch_mv(ADC_CHANNEL_ADC_VBAT);
        if (supercap_mv > WBEC_WBMZ6_SUPERCAP_VOLTAGE_MIN_MV) {
            ret = WBMZ6_DEVICE_SUPERCAP;
        }
    }

    return ret;
}

bool wbmz6_battery_is_present(void)
{
    return axp221s_is_present();
}

bool wbmz6_battery_init(void)
{
    uint16_t bat_capacity_reg_value = WBEC_WBMZ6_BATTERY_FULL_DESIGN_CAPACITY_MAH / 1.456;
    bat_capacity_reg_value |= 0x8000;   // total battery capacity is configured
    if (!axp221s_write_u16(AXP221S_REG_TOTAL_BAT_CAPACITY_1, bat_capacity_reg_value)) {
        return false;
    }

    for (unsigned i = 0; i < ARRAY_SIZE(wbmz6_init_sequence); i++) {
        if (!axp221s_write_u8(wbmz6_init_sequence[i][0], wbmz6_init_sequence[i][1])) {
            return false;
        }
    }
    return true;
}

void wbmz6_battery_update_params(struct wbmz6_params *params)
{
    params->voltage_min_mv = WBEC_WBMZ6_BATTERY_VOLTAGE_MIN_MV;
    params->voltage_max_mv = WBEC_WBMZ6_BATTERY_VOLTAGE_MAX_MV;
    params->charge_current_ma = WBEC_WBMZ6_BATTERY_CHARGE_CURRENT_MA;
    params->full_design_capacity_mah = WBEC_WBMZ6_BATTERY_FULL_DESIGN_CAPACITY_MAH;
}

void wbmz6_battery_update_status(struct wbmz6_status *status)
{
    status->voltage_now_mv = axp221s_read_u16(AXP221S_REG_BATTERY_VOLTAGE);
    status->current_now_ma = axp221s_read_u16(AXP221S_REG_BATTERY_CHARGE_CURRENT);
    status->capacity_percent = axp221s_read_u16(AXP221S_REG_BATTERY_LEVEL);
    status->temperature = axp221s_read_u16(AXP221S_TS_PIN_ADC_DATA);
}

void wbmz6_do_periodic_work(void)
{
    // enum wbmz6_device device_found = wmbz6_detect_device();

    // if (device_found != wbmz6_ctx.device) {
    //     wbmz6_ctx.device = device_found;
    //     switch (device_found) {
    //     case WBMZ6_DEVICE_BATTERY:
    //         // Init battery

    //         wbmz6_ctx.full_design_capacity_mah = WBEC_WBMZ6_BATTERY_FULL_DESIGN_CAPACITY_MAH;
    //         wbmz6_ctx.voltage_min_mv = WBEC_WBMZ6_BATTERY_VOLTAGE_MIN_MV;
    //         wbmz6_ctx.voltage_max_mv = WBEC_WBMZ6_BATTERY_VOLTAGE_MAX_MV;
    //         wbmz6_ctx.charge_current_ma = WBEC_WBMZ6_BATTERY_CHARGE_CURRENT_MA;

    //         axp221s_init();
    //         axp221s_set_battery_full_desing_capacity(wbmz6_ctx.full_design_capacity_mah);
    //         axp221s_set_battery_voltage_min(wbmz6_ctx.voltage_min_mv);
    //         axp221s_set_battery_voltage_max(wbmz6_ctx.voltage_max_mv);
    //         axp221s_set_battery_charging_current_max(wbmz6_ctx.charge_current_ma);

    //         break;

    //     case WBMZ6_DEVICE_SUPERCAP:
    //         // Init supercap

    //         break;

    //     default:
    //         break;

    //     }
    // }

}

#endif
