#include "i2c-slave.h"
#include "stm32g0xx.h"
#include "gpio.h"
#include "regmap.h"

#define I2C_SLAVE_SCL_PORT      GPIOB
#define I2C_SLAVE_SCL_PIN       6
#define I2C_SLAVE_SDA_PORT      GPIOB
#define I2C_SLAVE_SDA_PIN       7
#define I2C_SLAVE_AF            6

#define I2C_SLAVE_BUS           I2C1

enum i2c_operation {
    OP_SET_REG_ADDR,
    OP_SLAVE_TRANSMIT,
    OP_SLAVE_RECEIVE
};

static struct i2c_slave_ctx {
    enum i2c_operation op;
    uint8_t index;
} i2c_slave_ctx;

void I2C1_IRQHandler(void);

void i2c_slave_init(void)
{
    RCC->APBENR1 |= RCC_APBENR1_I2C1EN;

    GPIO_SET_OD(I2C_SLAVE_SCL_PORT, I2C_SLAVE_SCL_PIN);
    GPIO_SET_OUTPUT(I2C_SLAVE_SCL_PORT, I2C_SLAVE_SCL_PIN);
    GPIO_SET_AF(I2C_SLAVE_SCL_PORT, I2C_SLAVE_SCL_PIN, I2C_SLAVE_AF);

    GPIO_SET_OD(I2C_SLAVE_SDA_PORT, I2C_SLAVE_SDA_PIN);
    GPIO_SET_OUTPUT(I2C_SLAVE_SDA_PORT, I2C_SLAVE_SDA_PIN);
    GPIO_SET_AF(I2C_SLAVE_SDA_PORT, I2C_SLAVE_SDA_PIN, I2C_SLAVE_AF);

    I2C_SLAVE_BUS->OAR1 = (0x50 << 1) | I2C_OAR1_OA1EN;

    I2C_SLAVE_BUS->CR1 |= I2C_CR1_ADDRIE | I2C_CR1_TCIE | I2C_CR1_TXIE | I2C_CR1_NACKIE | I2C_CR1_STOPIE | I2C_CR1_SBC;

    I2C_SLAVE_BUS->CR1 |= I2C_CR1_PE;

    NVIC_EnableIRQ(I2C1_IRQn);
}

void i2c_slave_set_busy(bool busy)
{
    if (busy) {
        I2C_SLAVE_BUS->OAR1 &= ~I2C_OAR1_OA1EN;
    } else {
        I2C_SLAVE_BUS->OAR1 |= I2C_OAR1_OA1EN;
    }
}


void I2C1_IRQHandler(void)
{
    if (I2C_SLAVE_BUS->ISR & I2C_ISR_ADDR) {
        I2C_SLAVE_BUS->ISR = I2C_ISR_TXE;

        if (I2C_SLAVE_BUS->ISR & I2C_ISR_DIR) {
            // Slave transmitter
            I2C_SLAVE_BUS-> CR1 &= ~I2C_CR1_SBC;

            i2c_slave_ctx.op = OP_SLAVE_TRANSMIT;
        } else {
            // Slave receiver
            I2C_SLAVE_BUS-> CR1 |= I2C_CR1_SBC;

            I2C_SLAVE_BUS->CR2 &= ~I2C_CR2_NBYTES_Msk;
            I2C_SLAVE_BUS->CR2 |= 1 << I2C_CR2_NBYTES_Pos;

            I2C_SLAVE_BUS->CR2 |= I2C_CR2_RELOAD;

            i2c_slave_ctx.op = OP_SET_REG_ADDR;
        }

        I2C_SLAVE_BUS->ICR = I2C_ICR_ADDRCF;
    }

    if (I2C_SLAVE_BUS->ISR & I2C_ISR_TCR) {
        uint8_t rd = I2C_SLAVE_BUS->RXDR;

        switch (i2c_slave_ctx.op) {
        case OP_SET_REG_ADDR:
            if (rd > regmap_get_max_reg()) {
                I2C_SLAVE_BUS->CR2 |= I2C_CR2_NACK;
            } else {
                I2C_SLAVE_BUS->CR2 &= ~I2C_CR2_NACK;
                regmap_make_snapshot();
                i2c_slave_ctx.index = rd;
                i2c_slave_ctx.op = OP_SLAVE_RECEIVE;
            }
            break;

        case OP_SLAVE_RECEIVE:
            if (regmap_set_snapshot_reg(i2c_slave_ctx.index, rd)) {
                I2C_SLAVE_BUS->CR2 &= ~I2C_CR2_NACK;
            } else {
                I2C_SLAVE_BUS->CR2 |= I2C_CR2_NACK;
            }
            i2c_slave_ctx.index++;
            if (i2c_slave_ctx.index == sizeof(struct regmap)) {
                i2c_slave_ctx.index = 0;
            }
            break;

        default:
            I2C_SLAVE_BUS->CR2 |= I2C_CR2_NACK;
            break;
        }
        I2C_SLAVE_BUS->CR2 |= 1 << I2C_CR2_NBYTES_Pos;
    }

    if (I2C_SLAVE_BUS->ISR & I2C_ISR_TXIS) {
        uint8_t byte = 0xFF;
        if (i2c_slave_ctx.index < sizeof(struct regmap)) {
            byte = regmap_get_snapshot_reg(i2c_slave_ctx.index);
        }
        i2c_slave_ctx.index++;
        if (i2c_slave_ctx.index > regmap_get_max_reg()) {
            i2c_slave_ctx.index = 0;
        }
        I2C_SLAVE_BUS->TXDR = byte;

    }

    if (I2C_SLAVE_BUS->ISR & I2C_ISR_NACKF) {
        // If NACK received, index was incremented with previous byte because TXDR is buffered
        // Decrement index
        // TODO Find better solution
        i2c_slave_ctx.index--;
        if (i2c_slave_ctx.index > regmap_get_max_reg()) {
            i2c_slave_ctx.index = regmap_get_max_reg();
        }

        I2C_SLAVE_BUS->ICR = I2C_ICR_NACKCF;
    }

    if (I2C_SLAVE_BUS->ISR & I2C_ISR_STOPF) {
        regmap_set_write_completed();
        I2C_SLAVE_BUS->ICR = I2C_ICR_STOPCF;
    }
}
