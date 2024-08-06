#include "axp221s.h"
#include "config.h"
#include "software_i2c.h"

#if defined WBEC_WBMZ6_SUPPORT

#define AXP221S_ADDR                            0x68

#define AXP221S_REG_ADC_ENABLE                  0x82
#define AXP221S_REG_ADC_ADC_RATE                0x84
#define AXP221S_REG_VOFF_SETTING                0x31
#define AXP221S_REG_CHARGE_CONTROL_1            0x33


#define AXP221S_REG_INPUT_POWER_STATUS          0x00
#define AXP221S_REG_INPUT_POWER_STATUS_ACIN_PRESENCE     0xC5

#define AXP211S_REG_PWR_OP_MODE                 0x01

#define AXP221S_REG_BATTERY_LEVEL               0xB9

#define AXP221S_REG_BATTERY_CAPACITY_1          0xE0
#define AXP221S_REG_BATTERY_CAPACITY_2          0xE1

#define AXP221S_REG_BATTERY_VOLTAGE_1           0x78
#define AXP221S_REG_BATTERY_VOLTAGE_2           0x79

#define AXP221S_REG_BATTERY_CHARGE_CURRENT_1    0x7A
#define AXP221S_REG_BATTERY_CHARGE_CURRENT_2    0x7B

#define AXP221S_REG_BATTERY_DISCHARGE_CURRENT_1 0x7C
#define AXP221S_REG_BATTERY_DISCHARGE_CURRENT_2 0x7D

#define AXP221S_INTERNAL_TEMPERATURE_1          0x56
#define AXP221S_INTERNAL_TEMPERATURE_2          0x57

#define AXP221S_TS_PIN_ADC_DATA_1               0x58
#define AXP221S_TS_PIN_ADC_DATA_2               0x59

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
    },
};

static inline bool axp221s_write_u8(uint8_t reg, uint8_t value)
{
    software_i2c_status_t ret;
    ret = software_i2c_write(I2C_PORT_WBMZ6, AXP221S_ADDR, reg, 1, &value, 1);
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
    ret = software_i2c_write(I2C_PORT_WBMZ6, AXP221S_ADDR, reg, 1, buf, 2);
    if (ret == I2C_STATUS_OK) {
        return true;
    }
    return false;
}


static inline uint8_t axp221s_read_u8(uint8_t reg)
{
    uint8_t value;
    software_i2c_read_after_write(I2C_PORT_WBMZ6, AXP221S_ADDR, &reg, 1, &value, 1);
    return value;
}


static inline uint16_t axp221s_read_u16(uint8_t reg)
{
    uint8_t buf[2];
    software_i2c_read_after_write(I2C_PORT_WBMZ6, AXP221S_ADDR, &reg, 1, buf, 2);
    return buf[0] | (buf[1] << 8);
}

void axp221s_init(void)
{
    software_i2c_init();


    /**
     * + w 82 -> e1
     * + r 84 -> 32
     *
     * + r 31 -> 03
     * + r 33 -> c6
     * + w 33 -> c2
     *
     * r 00 -> c5
     * r 01 -> 30
     * r 01 -> 30
     * r 00 -> c5
     * r 78 -> f0
     * r 79 -> 08
     * r 00 -> c5
     * r 7a -> 00
     * r 7b -> 00
     * r 01 -> 30
     * r 01 -> 30
     * r b9 -> e4
     * 
     * 
     * 
     */
}

bool axp221s_1is_present(void)
{
    if (software_i2c_detect_device(I2C_PORT_WBMZ6, AXP221S_ADDR) == I2C_STATUS_OK) {
        return true;
    }
    return false;
}

bool axp221s_set_battery_params(uint16_t capacity_mah, uint16_t vmin_mv, uint16_t vmax_mv, uint16_t charge_current_ma)
{
    if (!axp221s_write_u8(AXP221S_REG_ADC_ENABLE, 0xE1)) {
        return false;
    }

}

#endif