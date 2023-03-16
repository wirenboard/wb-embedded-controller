#include "spi-slave.h"
#include "config.h"
#include "gpio.h"
#include "regmap.h"

#define SPI_SLAVE_OPERATION_READ_MASK            0x8000

enum spi_slave_op {
    SPI_SLAVE_INIT_OP,
    SPI_SLAVE_RECEIVE,
    SPI_SLAVE_TRANSMIT,
};

struct spi_slave_ctx {
    enum spi_slave_op op;
};

static struct spi_slave_ctx spi_slave_ctx;

static inline void spi_tx_u8(uint8_t byte)
{
    *(__IO uint8_t *)(&SPI2->DR) = byte;
}

static inline void spi_tx_u16(uint16_t word)
{
    *(__IO uint16_t *)(&SPI2->DR) = word;
}

static inline uint16_t spi_rd_u16(void)
{
    return *(__IO uint16_t *)(&SPI2->DR);
}

static inline void spi_enable_txe_int(void)
{
    SPI2->CR2 |= SPI_CR2_TXEIE;
}

static inline void reset_and_init_spi(void)
{
    // Disable SPI
    SPI2->CR1 = 0;

    // Full reset SPI: flush FIFOs
    RCC->APBRSTR1 |= RCC_APBRSTR1_SPI2RST;
    RCC->APBRSTR1 &= ~RCC_APBRSTR1_SPI2RST;

    // SPI2->CR2 |= 0b0111 << SPI_CR2_DS_Pos;   // 8 bit
    SPI2->CR2 |= 0b1111 << SPI_CR2_DS_Pos;   // 8 bit
    SPI2->CR2 |= /* SPI_CR2_TXEIE | */ SPI_CR2_RXNEIE;
    //SPI2->CR2 |= SPI_CR2_FRXTH;
    SPI2->CR1 |= SPI_CR1_SPE;

    // Put 2 dummy bytes to TX FIFO
    // spi_tx_u8(0x55);
    spi_tx_u16(0xAA55);

    spi_slave_ctx.op = SPI_SLAVE_INIT_OP;
}

void spi_slave_init(void)
{
    // TODO Remove debug
    GPIO_SET_OUTPUT(GPIOD, 0);
    GPIO_SET_OUTPUT(GPIOD, 1);

    GPIO_SET_OUTPUT(GPIOB, 6);      // MISO
    GPIO_SET_INPUT(GPIOB, 7);       // MOSI
    GPIO_SET_INPUT(GPIOB, 8);       // SCK
    GPIO_SET_INPUT(GPIOB, 9);       // CS

    GPIO_SET_AF(GPIOB, 6, 4);       // MISO
    GPIO_SET_AF(GPIOB, 7, 1);       // MOSI
    GPIO_SET_AF(GPIOB, 8, 1);       // SCK
    GPIO_SET_AF(GPIOB, 9, 5);       // CS

    // Init rising exti on ~CS
    RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
    EXTI->RTSR1 |= (1 << 9);
    EXTI->IMR1 |= (1 << 9);
    EXTI->EXTICR[9 / 4] |= ((uint32_t)0x01 << ((9 % 4) * 8));

    NVIC_EnableIRQ(EXTI4_15_IRQn);
    NVIC_SetPriority(EXTI4_15_IRQn, 0);

    RCC->APBENR1 |= RCC_APBENR1_SPI2EN;

    reset_and_init_spi();

    NVIC_EnableIRQ(SPI2_IRQn);
    NVIC_SetPriority(SPI2_IRQn, 0);
}

void SPI2_IRQHandler(void)
{
    if (SPI2->SR & SPI_SR_RXNE) {
        // TODO Remove debug
        GPIO_SET(GPIOD, 0);

        uint16_t rd = spi_rd_u16();
        if (spi_slave_ctx.op == SPI_SLAVE_INIT_OP) {
            uint16_t addr = rd & ~SPI_SLAVE_OPERATION_READ_MASK;
            regmap_ext_prepare_operation(addr);
            if (rd & SPI_SLAVE_OPERATION_READ_MASK) {
                spi_slave_ctx.op = SPI_SLAVE_TRANSMIT;
                spi_enable_txe_int();
            } else {
                spi_slave_ctx.op = SPI_SLAVE_RECEIVE;
            }
        } else if (spi_slave_ctx.op == SPI_SLAVE_RECEIVE) {
            regmap_ext_write_reg_autoinc(rd);
        }

        // TODO Remove debug
        GPIO_RESET(GPIOD, 0);
    }

    if (SPI2->SR & SPI_SR_TXE) {
        // TODO Remove debug
        GPIO_SET(GPIOD, 1);

        uint16_t w = 0;
        if (spi_slave_ctx.op == SPI_SLAVE_TRANSMIT) {
            w = regmap_ext_read_reg_autoinc();
            // uint16_t w = spi_slave_ctx.reg_addr + 10;
        }
        spi_tx_u16(w);

        // TODO Remove debug
        GPIO_RESET(GPIOD, 1);
    }
}

void EXTI4_15_IRQHandler(void)
{
    if (EXTI->RPR1 & EXTI_RPR1_RPIF9) {
        EXTI->RPR1 = EXTI_RPR1_RPIF9;
        regmap_ext_end_operation();
        reset_and_init_spi();
    }
}
