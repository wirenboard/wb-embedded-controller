#include "software_i2c.h"

#if defined (SOFTWARE_I2C_DESC)

#include <stdbool.h>
#include "wbmcu_system.h"
#include "rcc.h"
#include "gpio.h"
// #include "failure_panic.h"
#include "array_size.h"


#define I2C_BUS_RESET_MAX_PERIODS                           100
#define I2C_BUS_WAIT_STRETCHING_TIMES                       1000

#define I2C_CMD_WRITE(a)                                    (((a) << 1) + 0)
#define I2C_CMD_READ(a)                                     (((a) << 1) + 1)

struct software_i2c_context {
    __IO uint32_t * sda_bsrr;
    __IO uint32_t * sda_idr;
    uint32_t sda_set;
    uint32_t sda_reset;
    __IO uint32_t * scl_bsrr;
    __IO uint32_t * scl_idr;
    uint32_t scl_set;
    uint32_t scl_reset;
};

struct software_i2c_delay {
    void (*rl)(void);
    void (*rh)(void);
    void (*wl)(void);
    void (*wh)(void);
};

// delay functions
static void software_i2c_bit_delay_nop_0(void)
{
}

static void software_i2c_bit_delay_nop_2(void)
{
    __NOP(); __NOP();
}

static void software_i2c_bit_delay_nop_4(void)
{
    __NOP(); __NOP(); __NOP(); __NOP();
}

static void software_i2c_bit_delay_nop_8(void)
{
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
}

static void software_i2c_bit_delay_nop_16(void)
{
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
}

static void software_i2c_bit_delay_nop_38(void)
{
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
}

static void software_i2c_bit_delay_nop_for_100khz(void)
{
    for (volatile uint8_t i = 0; i < 14; i++) {
        __NOP();
    }
}

// экспериментально подобрана для MAP FRAM 1 MHz @ 48 MHz CPU
static const struct software_i2c_delay delay_1M_48MHz = {
    .rh = software_i2c_bit_delay_nop_4,
    .wh = software_i2c_bit_delay_nop_8,
    .rl = software_i2c_bit_delay_nop_2,
    .wl = software_i2c_bit_delay_nop_0,
};

// экспериментально подобрана для MAP EEPROM ~380 KHz @ 48 MHz CPU
static const struct software_i2c_delay delay_400K_48MHz = {
    .rh = software_i2c_bit_delay_nop_38,
    .wh = software_i2c_bit_delay_nop_38,
    .rl = software_i2c_bit_delay_nop_38,
    .wl = software_i2c_bit_delay_nop_38,
};

// экспериментально подобрана для MR PCF8583 ~100 KHz @ 48 MHz CPU
static const struct software_i2c_delay delay_100K_48MHz = {
    .rh = software_i2c_bit_delay_nop_for_100khz,
    .wh = software_i2c_bit_delay_nop_for_100khz,
    .rl = software_i2c_bit_delay_nop_for_100khz,
    .wl = software_i2c_bit_delay_nop_for_100khz,
};

// максимум на данной реализации ~ 170 KHz
static const struct software_i2c_delay delay_400K_8MHz = {
    .rh = software_i2c_bit_delay_nop_0,
    .wh = software_i2c_bit_delay_nop_0,
    .rl = software_i2c_bit_delay_nop_0,
    .wl = software_i2c_bit_delay_nop_0,
};

// экспериментально подобрана для MS датчиков ~93 KHz @ 8 MHz CPU
// по какойто причине SGPC3 не работает на частоте больше 100 КГц
static const struct software_i2c_delay delay_100K_8MHz = {
    .rh = software_i2c_bit_delay_nop_16,
    .wh = software_i2c_bit_delay_nop_16,
    .rl = software_i2c_bit_delay_nop_16,
    .wl = software_i2c_bit_delay_nop_16,
};

// init structures and context switcher


#define SOFTWARE_I2C_STRUCT(name, freq, sda_port, sda_pin, scl_port, scl_pin) \
    { \
        {sda_port, sda_pin}, \
        {scl_port, scl_pin}, \
        freq, \
    }

struct software_i2c_desc {
    gpio_pin_t sda;
    gpio_pin_t scl;
    uint32_t freq;
};

static struct software_i2c_context context;


static const struct software_i2c_desc i2c_desc[] = {
    SOFTWARE_I2C_DESC(SOFTWARE_I2C_STRUCT)
};

void software_i2c_init(void)
{
    for (uint8_t i = 0; i < ARRAY_SIZE(i2c_desc); i++) {
        /**
         * Пины PA13 и PA14 по дефолту находятся в режиме SWD.
         * Для работы software_i2c их нужно переключить в режим GPIO open-drain output pull-up.
         *
         * Но оказалось, что порядок переключения очень важен: сначала нужно переключить GPIO из AF в OUTPUT
         * и только потом менять остальные настройки, такие как open-drain, pull-up и т.д.
         * Если нарушить порядок, на GD32 наблюдались случаи зависания микроконтроллера. Причём из зависшего состояния
         * МК не выводится даже через RESET.
         * Предположение, что переходные процессы на линиях SWD при смене настроек переводят модуль SWD в некорректное состояние,
         * что вызывает зависание МК.
         * Также добавление небольшой ёмкости на линии SWD помогает устранить зависания, что в какой-то мере подтверждает предположение.
         */

        GPIO_S_SET_OUTPUT(i2c_desc[i].sda);
        GPIO_S_SET_OD(i2c_desc[i].sda);
        GPIO_S_SET_PULLUP(i2c_desc[i].sda);
        GPIO_S_SET(i2c_desc[i].sda);
        GPIO_S_SET_SPEED_HIGH(i2c_desc[i].sda);

        GPIO_S_SET_OUTPUT(i2c_desc[i].scl);
        GPIO_S_SET_OD(i2c_desc[i].scl);
        GPIO_S_SET_PULLUP(i2c_desc[i].scl);
        GPIO_S_SET(i2c_desc[i].scl);
        GPIO_S_SET_SPEED_HIGH(i2c_desc[i].scl);
    }
}

static const struct software_i2c_delay * delay;

static void software_i2c_delay_configure(uint32_t f)
{
    switch (SystemCoreClock) {
    case 48000000:
        if (f == 1000000) {
            delay = &delay_1M_48MHz;
        } else if (f == 400000) {
            delay = &delay_400K_48MHz;
        } else if (f == 100000) {
            delay = &delay_100K_48MHz;
        } else {
            // failure_panic(FAIL_COMMON);
        }
        break;

    case 8000000:
        if (f == 400000) {
            delay = &delay_400K_8MHz;
        } else if (f == 100000) {
            delay = &delay_100K_8MHz;
        } else {
            // failure_panic(FAIL_COMMON);
        }
    break;

    default:
        // failure_panic(FAIL_COMMON);
        // TODO: remove failure_panic
        // TODO: calc delays
        delay = &delay_1M_48MHz;
        break;
    }
}

static inline void software_i2c_set_context(enum software_i2c_ports p)
{
    context.sda_bsrr = &i2c_desc[p].sda.port->BSRR;
    context.sda_idr = &i2c_desc[p].sda.port->IDR;
    context.sda_set = 1 << (i2c_desc[p].sda.pin + 0);
    context.sda_reset = 1 << (i2c_desc[p].sda.pin + 16);

    context.scl_bsrr = &i2c_desc[p].scl.port->BSRR;
    context.scl_idr = &i2c_desc[p].scl.port->IDR;
    context.scl_set = 1 << (i2c_desc[p].scl.pin + 0);
    context.scl_reset = 1 << (i2c_desc[p].scl.pin + 16);

    software_i2c_delay_configure(i2c_desc[p].freq);
}

__attribute__((always_inline)) static inline unsigned software_i2c_get_sda(void) { return *context.sda_idr & context.sda_set; };
__attribute__((always_inline)) static inline void software_i2c_sda_high(void)    { *context.sda_bsrr = context.sda_set; };
__attribute__((always_inline)) static inline void software_i2c_sda_low(void)     { *context.sda_bsrr = context.sda_reset; };

__attribute__((always_inline)) static inline unsigned software_i2c_get_scl(void) { return *context.scl_idr & context.scl_set; };
__attribute__((always_inline)) static inline void software_i2c_scl_high(void)    { *context.scl_bsrr = context.scl_set; };
__attribute__((always_inline)) static inline void software_i2c_scl_low(void)     { *context.scl_bsrr = context.scl_reset; };

// i2c transaction elements

static bool software_i2c_check_bus_idle(void)
{
    delay->wl();
    software_i2c_sda_high();
    software_i2c_scl_high();
    delay->wh();
    if (software_i2c_get_sda() == 0) {
        return false;
    }
    return true;
}

static unsigned software_i2c_reset_bus_idle(void)
{
    unsigned t = I2C_BUS_RESET_MAX_PERIODS;
    while (t--) {
        if (software_i2c_get_sda()) {
            return 0;
        }
        software_i2c_scl_low();
        delay->wl();
        software_i2c_scl_high();
        delay->wh();
    }
    return 1;
}

#if defined USE_I2C_CLOCK_STRETCHING
    static unsigned software_i2c_wait_clock_stretching(void)
    {
        unsigned t = I2C_BUS_WAIT_STRETCHING_TIMES;
        while (t--) {
            if (software_i2c_get_scl()) {
                return 0;
            }
        }
        return 1;
    }

#else
    static inline unsigned software_i2c_wait_clock_stretching(void)
    {
        return 0;
    }

#endif

static void software_i2c_start(void)
{
    software_i2c_sda_low();
    delay->wh();
    software_i2c_scl_low();
    delay->wh();
}

static void software_i2c_stop(void)
{
    software_i2c_sda_low();
    delay->wh();
    software_i2c_scl_high();
    delay->wh();
    software_i2c_sda_high();
    delay->wh();
}

static unsigned software_i2c_write_byte(uint8_t data)
{
    unsigned ack;
    unsigned mask = 0x40;
    software_i2c_scl_low();

    // msb передается не в цикле, чтобы инструкции цикла попадали на верхний уровень scl
    if (data & 0x80) {
        software_i2c_sda_high();
    } else {
        software_i2c_sda_low();
    }
    delay->wl();
    software_i2c_scl_high();
    if (software_i2c_wait_clock_stretching()) {
        return I2C_STATUS_CLOCK_STRETCHING_FAIL;
    }

    while (mask) {
        // в цикле передается только 7 бит
        delay->wh();
        software_i2c_scl_low();
        if (data & mask) {
            software_i2c_sda_high();
        } else {
            software_i2c_sda_low();
        }
        delay->wl();
        software_i2c_scl_high();
        mask >>= 1;
    }
    delay->wh();
    software_i2c_scl_low();

    // цикл scl low->delay->high->delay->read->low для чтения ack
    software_i2c_sda_high();
    delay->wl();
    software_i2c_scl_high();
    delay->wh();
    ack = !software_i2c_get_sda();
    software_i2c_scl_low();
    if (ack == 0) {
        return I2C_STATUS_DATA_NACK;
    }
    return I2C_STATUS_OK;
}

static unsigned software_i2c_read_byte(uint8_t * data, uint8_t ack)
{
    unsigned mask = 0x40;
    software_i2c_scl_low();
    software_i2c_sda_high();

    delay->rl();
    software_i2c_scl_high();
    if (software_i2c_wait_clock_stretching()) {
        return I2C_STATUS_CLOCK_STRETCHING_FAIL;
    }
    delay->rh();
    *data = 0;
    if (software_i2c_get_sda()) {
        *data |= 0x80;
    }
    software_i2c_scl_low();

    while (mask) {
        delay->rl();
        software_i2c_scl_high();
        delay->rh();
        if (software_i2c_get_sda()) {
            *data |= mask;
        }
        software_i2c_scl_low();
        mask >>= 1;
    }
    if (ack) {
        software_i2c_sda_low();
    }
    delay->rl();
    software_i2c_scl_high();
    delay->rh();
    software_i2c_scl_low();
    software_i2c_sda_high();
    return I2C_STATUS_OK;
}

software_i2c_status_t software_i2c_write(enum software_i2c_ports p, uint8_t slaveid, const uint8_t * address, uint8_t address_len, const uint8_t * txbuf, uint16_t txlen)
{
    unsigned ret;
    software_i2c_set_context(p);
    if (!software_i2c_check_bus_idle()) {
        if (software_i2c_reset_bus_idle()) {
            return I2C_STATUS_BUS_RESET_FAIL;
        }
        // increment error counter
    }
    software_i2c_start();

    ret = software_i2c_write_byte(I2C_CMD_WRITE(slaveid));
    if (ret != I2C_STATUS_OK) {
        software_i2c_stop();
        if (ret == I2C_STATUS_DATA_NACK) {
            return I2C_STATUS_SLAVE_ID_NACK;
        }
        return ret;
    }

    while (address_len--) {
        ret = software_i2c_write_byte(address[address_len]);
        if (ret != I2C_STATUS_OK) {
            software_i2c_stop();
            return ret;
        }
    }

    while (txlen--) {
        ret = software_i2c_write_byte(*txbuf++);
        if (ret != I2C_STATUS_OK) {
            software_i2c_stop();
            return ret;
        }
    }

    software_i2c_stop();
    return I2C_STATUS_OK;
}

software_i2c_status_t software_i2c_read_after_write(enum software_i2c_ports p, uint8_t slaveid, const uint8_t * address, uint8_t address_len, uint8_t * rxbuf, uint16_t rxlen)
{
    unsigned ret;
    software_i2c_set_context(p);
    if (!software_i2c_check_bus_idle()) {
        if (software_i2c_reset_bus_idle()) {
            return I2C_STATUS_BUS_RESET_FAIL;
        }

        // increment error counter
    }
    software_i2c_start();

    ret = software_i2c_write_byte(I2C_CMD_WRITE(slaveid));
    if (ret != I2C_STATUS_OK) {
        software_i2c_stop();
        if (ret == I2C_STATUS_DATA_NACK) {
            return I2C_STATUS_SLAVE_ID_NACK;
        }
        return ret;
    }

    while (address_len--) {
        ret = software_i2c_write_byte(address[address_len]);
        if (ret != I2C_STATUS_OK) {
            software_i2c_stop();
            return ret;
        }
    }

    if (!software_i2c_check_bus_idle()) {
        if (software_i2c_reset_bus_idle()) {
            return I2C_STATUS_BUS_RESET_FAIL;
        }

        // increment error counter
    }
    software_i2c_start();

    ret = software_i2c_write_byte(I2C_CMD_READ(slaveid));
    if (ret != I2C_STATUS_OK) {
        software_i2c_stop();
        if (ret == I2C_STATUS_DATA_NACK) {
            return I2C_STATUS_SLAVE_ID_NACK;
        }
        return ret;
    }

    rxlen--;
    while (rxlen--) {
        ret = software_i2c_read_byte(rxbuf++, 1);
        if (ret != I2C_STATUS_OK) {
            software_i2c_stop();
            return ret;
        }
    }

    ret = software_i2c_read_byte(rxbuf, 0);
    if (ret != I2C_STATUS_OK) {
        software_i2c_stop();
        return ret;
    }

    software_i2c_stop();
    return I2C_STATUS_OK;
}

software_i2c_status_t software_i2c_transaction(enum software_i2c_ports p, uint8_t slaveid, const uint8_t * txbuf, uint16_t txlen, uint8_t * rxbuf, uint16_t rxlen)
{
    unsigned ret;
    software_i2c_set_context(p);
    if (!software_i2c_check_bus_idle()) {
        if (software_i2c_reset_bus_idle()) {
            return I2C_STATUS_BUS_RESET_FAIL;
        }

        // increment error counter
    }
    software_i2c_start();

    if (txlen) {

        ret = software_i2c_write_byte(I2C_CMD_WRITE(slaveid));
        if (ret != I2C_STATUS_OK) {
            software_i2c_stop();
            if (ret == I2C_STATUS_DATA_NACK) {
                return I2C_STATUS_SLAVE_ID_NACK;
            }
            return ret;
        }

        while (txlen--) {
            ret = software_i2c_write_byte(*txbuf++);
            if (ret != I2C_STATUS_OK) {
                software_i2c_stop();
                return ret;
            }
        }

        //restart
        if (rxlen) {
            if (!software_i2c_check_bus_idle()) {
                if (software_i2c_reset_bus_idle()) {
                    return I2C_STATUS_BUS_RESET_FAIL;
                }

                // increment error counter
            }
            software_i2c_start();
        }
    }

    if (rxlen) {

        ret = software_i2c_write_byte(I2C_CMD_READ(slaveid));
        if (ret != I2C_STATUS_OK) {
            software_i2c_stop();
            if (ret == I2C_STATUS_DATA_NACK) {
                return I2C_STATUS_SLAVE_ID_NACK;
            }
            return ret;
        }

        rxlen--;
        while (rxlen--) {
            ret = software_i2c_read_byte(rxbuf++, 1);
            if (ret != I2C_STATUS_OK) {
                software_i2c_stop();
                return ret;
            }
        }

        ret = software_i2c_read_byte(rxbuf, 0);
        if (ret != I2C_STATUS_OK) {
            software_i2c_stop();
            return ret;
        }
    }

    software_i2c_stop();
    return I2C_STATUS_OK;
}

#endif
