#pragma once
#include <stdint.h>
#include "config.h"

/*
necessary defines in config.h
    SOFTWARE_I2C_DESC(macro)    macro(name, freq_hz, sda_gpio, sda_pin, scl_gpio, scl_pin)

SUPPORT ONLY:
    clock 48MHz:    1000000 (1 MHz), 400000 (400 KHz)
    clock 8MHz:     400000 (400 KHz), 100000 (100 KHz)

optional
    SOFTWARE_I2C_SINGLE_BUS - for compile time gpio and delay instruction optimisation NOT TESTED!
    USE_I2C_CLOCK_STRETCHING - for check scl stratching after ACK (for sgpc3)

*/

#if defined (SOFTWARE_I2C_DESC)

#define SOFTWARE_I2C_ENUM(name, freq, sda_port, sda_pin, scl_port, scl_pin)                  I2C_PORT_##name

enum software_i2c_ports {
    SOFTWARE_I2C_DESC(SOFTWARE_I2C_ENUM)
};

typedef enum {
    I2C_STATUS_OK = 0,
    I2C_STATUS_SLAVE_ID_NACK,
    I2C_STATUS_DATA_NACK,
    I2C_STATUS_CLOCK_STRETCHING_FAIL,
    I2C_STATUS_BUS_RESET_FAIL,
} software_i2c_status_t;

void software_i2c_init(void);

software_i2c_status_t software_i2c_transaction(enum software_i2c_ports p, uint8_t slaveid, const uint8_t * txbuf, uint16_t txlen, uint8_t * rxbuf, uint16_t rxlen);

// helpers for transaction with separated address and data buffers. addres send msb first.
software_i2c_status_t software_i2c_write(enum software_i2c_ports p, uint8_t slaveid, const uint8_t * address, uint8_t address_len, const uint8_t * txbuf, uint16_t txlen);
// DONT USE THIS FUNCTIONS WITH addres_len = 0;
software_i2c_status_t software_i2c_read_after_write(enum software_i2c_ports p, uint8_t slaveid, const uint8_t * address, uint8_t address_len, uint8_t * rxbuf, uint16_t rxlen);

// проверка наличия на шине устройства с адресом slaveid
static inline software_i2c_status_t software_i2c_detect_device(enum software_i2c_ports p, uint8_t slaveid)
{
    return software_i2c_write(p, slaveid, 0, 0, 0, 0);
}

#endif
