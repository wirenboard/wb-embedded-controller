#include "config.h"

#if defined WBEC_WBMZ6_SUPPORT
#include "adc.h"
#include "software_i2c.h"
#include <assert.h>
#include "array_size.h"
#include "wbmz6-battery.h"
#include "ntc.h"

static_assert(WBEC_WBMZ6_BATTERY_VOLTAGE_MIN_MV == 2900, "Only 2900 mV min voltage supported");
static_assert(WBEC_WBMZ6_BATTERY_VOLTAGE_MAX_MV == 4100, "Only 4100 mV max voltage supported");
static_assert(WBEC_WBMZ6_BATTERY_CHARGE_CURRENT_MA == 600, "Only 600 mA charge current supported");

#define AXP221S_ADDR                                0x34

#define AXP221S_REG_ADC_ENABLE                      0x82
#define AXP221S_REG_ADC_ADC_RATE                    0x84
#define AXP221S_REG_VOFF_SETTING                    0x31
#define AXP221S_REG_CHARGE_CONTROL_1                0x33
#define AXP221S_REG_TOTAL_BAT_CAPACITY_1            0xE0
#define AXP221S_REG_TOTAL_BAT_CAPACITY_2            0xE1

#define AXP221S_POWER_STATUS                        0x00
#define AXP221S_OP_MODE                             0x01
#define AXP221S_TS_PIN_ADC_DATA                     0x58
#define AXP221S_REG_BATTERY_VOLTAGE                 0x78
#define AXP221S_REG_BATTERY_CHARGE_CURRENT          0x7A
#define AXP221S_REG_BATTERY_DISCHARGE_CURRENT       0x7C
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
    buf[0] = value >> 8;
    buf[1] = value & 0xFF;
    ret = software_i2c_write(I2C_PORT_WBMZ6, AXP221S_ADDR, &reg, 1, buf, 2);
    if (ret == I2C_STATUS_OK) {
        return true;
    }
    return false;
}

static inline uint16_t axp221s_read_u8(uint8_t reg)
{
    uint8_t buf;
    software_i2c_read_after_write(I2C_PORT_WBMZ6, AXP221S_ADDR, &reg, 1, &buf, 1);
    return buf;
}

static inline uint16_t axp221s_read_u16(uint8_t reg)
{
    uint8_t buf[2];
    software_i2c_read_after_write(I2C_PORT_WBMZ6, AXP221S_ADDR, &reg, 1, buf, 2);
    return (buf[0] << 8) | buf[1];
}

static inline void axp221s_read_buf(uint8_t reg, uint8_t *buf, uint8_t len)
{
    software_i2c_read_after_write(I2C_PORT_WBMZ6, AXP221S_ADDR, &reg, 1, buf, len);
}

static inline uint16_t axp221s_get_adc_value_from_buf(uint8_t buf[2], uint8_t bits_in_first_reg, uint8_t bits_in_second_reg)
{
    buf[0] &= (1 << bits_in_first_reg) - 1;
    buf[1] &= (1 << bits_in_second_reg) - 1;

    return (buf[0] << bits_in_second_reg) | buf[1];
}

static inline uint16_t axp221s_read_adc_value(uint8_t reg, uint8_t bits_in_first_reg, uint8_t bits_in_second_reg)
{
    uint8_t buf[2];
    software_i2c_read_after_write(I2C_PORT_WBMZ6, AXP221S_ADDR, &reg, 1, buf, 2);

    return axp221s_get_adc_value_from_buf(buf, bits_in_first_reg, bits_in_second_reg);
}

bool wbmz6_battery_is_present(void)
{
    return axp221s_is_present();
}

bool wbmz6_battery_init(void)
{
    // 0x8000 - total battery capacity is configured
    // 1 lsb = 1.456 mAh
    const uint16_t bat_capacity_reg_value = 0x8000 | (uint16_t)(WBEC_WBMZ6_BATTERY_FULL_DESIGN_CAPACITY_MAH / 1.456);
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
    // read voltage, charging current, discharging current in one transaction
    uint8_t buf[6];
    axp221s_read_buf(AXP221S_REG_BATTERY_VOLTAGE, buf, 6);

    uint32_t v = axp221s_get_adc_value_from_buf(&buf[0], 8, 4);
    // lsb is 1.1mV
    v = (v * 1100) / 1000;
    status->voltage_now_mv = v;

    // lsb is 0.5mA
    status->charging_current_ma = axp221s_get_adc_value_from_buf(&buf[2], 8, 5) >> 1;
    status->discharging_current_ma = axp221s_get_adc_value_from_buf(&buf[4], 8, 5) >> 1;

    // lsb is 1%
    status->capacity_percent = axp221s_read_u8(AXP221S_REG_BATTERY_LEVEL);
    status->capacity_percent &= 0x7F;   // clear bit 7 - calculation status flag

    fix16_t ts_adc = fix16_from_int(axp221s_read_adc_value(AXP221S_TS_PIN_ADC_DATA, 8, 4));
    fix16_t ts_mv = fix16_mul(ts_adc, F16(0.8));
    fix16_t ntc_kohm = fix16_div(ts_mv, F16(80));
    ntc_kohm = fix16_sub(ntc_kohm, F16(2));  // 2 kohm series resistor
    fix16_t temp = ntc_kohm_to_temp(ntc_kohm);
    status->temperature = fix16_to_int(fix16_mul(temp, F16(10)));

    // read POWER_STATUS and OP_MODE in one transaction
    axp221s_read_buf(AXP221S_POWER_STATUS, buf, 2);
    status->is_charging = (buf[0] & 0x04) ? true : false;
    status->is_dead = (buf[1] & 0x08) ? true : false;
    status->is_inserted = (buf[1] & 0x10) ? true : false;
}

#endif
