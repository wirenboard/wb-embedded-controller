#include "spi-slave.h"
#include "array_size.h"
#include "gpio.h"
#include "regmap-ext.h"

/**
 * Реализация SPI Slave с размером слова 16 бит
 *
 * Используются SPI2 и GPIO:
 *  - PB6 - MISO
 *  - PB7 - MOSI
 *  - PB8 - SCK
 *  - PB9 - ~CS
 *
 * Поддержваются 2 операции:
 *  - чтение реигстров
 *  - запись регистров
 *
 * В первом слове Master передает адрес регистра (15 младших бит) и бит чтения/записи.
 * После приема адреса регистра устройство готовит данные для чтения, это занимает ~XX мкс на 64 МГц
 * Соответственно Master должен выдержать паузу между отправкой адреса и чтением/запись, либо
 * использовать частоту SPI, при которой период SCK будет больше этого времени.
 *
 * После записи адреса Master либо передает данные и получает в ответ нули (если это запись),
 * либо передает что угодно и получает в ответ данные, если это чтение.
 * За один раз может быть считано или записано сколько угодна данных, адрес инкрементируется
 *
 * Из-за особенной периферии STM, SPI приходится сбрасывать каждый раз при фронте на ~CS, чтобы
 * очистить очередь передачи, иначе при следующем обмене будут переданы старые данные.
 * Сброс делается через RCC регистры.
 * На линию ~CS настроено прерывание EXTI по фронту.
 */


// Маска бита чтения. Если в первом слове этот бит равен 1, то это чтение
#define SPI_SLAVE_OPERATION_READ_MASK            0x8000
// Слово, которое передается в ответ на запись адреса
#define SPI_SLAVE_ADDR_WRITE_ANSWER              0x0000
// Количество незначащих слов между записью адреса и началом передачи данных. Нужно, чтобы подготовить данные
// Для совместимости со старым протоколом используется только с адреса 0x100 (для UART-ов)
#define SPI_SLAVE_PAD_WORDS_COUNT                5
#define SPI_SLAVE_USE_PAD_WORDS_SINCE_ADDR       0x110

static const struct spi_pins {
    gpio_pin_t miso;
    gpio_pin_t mosi;
    gpio_pin_t sck;
    gpio_pin_t cs;
} spi_pins = {
    .miso = {GPIOB, 6},
    .mosi = {GPIOB, 7},
    .sck = {GPIOB, 8},
    .cs = {GPIOB, 9},
};

enum spi_slave_op {
    SPI_SLAVE_ADDR_WRITE,   // Прием адреса
    SPI_SLAVE_RECEIVE,      // Получениче данных (запись Master -> Slave)
    SPI_SLAVE_TRANSMIT,     // Передача данных (чтение Svale -> Master)
};

static enum spi_slave_op spi_op;
static unsigned spi_tx_pad_words_cnt;
static unsigned spi_rx_pad_words_cnt;

static void spi_irq_handler(void);
static void exti_irq_handler(void);

// Запись 16-битного слова в очередь передачи SPI
static inline void spi_tx_u16(uint16_t word)
{
    *(__IO uint16_t *)(&SPI2->DR) = word;
}

// Чтение 16-битного слова из очереди приема SPI
static inline uint16_t spi_rd_u16(void)
{
    return *(__IO uint16_t *)(&SPI2->DR);
}

// Включение перывания по TXE
static inline void spi_enable_txe_int(void)
{
    SPI2->CR2 |= SPI_CR2_TXEIE;
}

// Сброс и повторная инициализация SPI
// Нужно для того, чтобы очистить очередь передачи и подготовить SPI к следующему обмену
static inline void reset_and_init_spi(void)
{
    // Disable SPI
    SPI2->CR1 = 0;

    // Full reset SPI: flush FIFOs
    RCC->APBRSTR1 |= RCC_APBRSTR1_SPI2RST;
    RCC->APBRSTR1 &= ~RCC_APBRSTR1_SPI2RST;

    SPI2->CR2 = (0b1111 << SPI_CR2_DS_Pos) | SPI_CR2_RXNEIE;   // 16 bit
    SPI2->CR1 = SPI_CR1_SPE;

    // Put dummy word to TX FIFO
    spi_tx_u16(SPI_SLAVE_ADDR_WRITE_ANSWER);

    spi_op = SPI_SLAVE_ADDR_WRITE;
    spi_rx_pad_words_cnt = 0;
    spi_tx_pad_words_cnt = 0;
}

void spi_slave_init(void)
{
    // Init SPI GPIOs
    GPIO_S_SET_OUTPUT(spi_pins.miso);
    GPIO_S_SET_INPUT(spi_pins.mosi);
    GPIO_S_SET_INPUT(spi_pins.sck);
    GPIO_S_SET_INPUT(spi_pins.cs);

    GPIO_S_SET_AF(spi_pins.miso, 4);
    GPIO_S_SET_AF(spi_pins.mosi, 1);
    GPIO_S_SET_AF(spi_pins.sck, 1);
    GPIO_S_SET_AF(spi_pins.cs, 5);

    // Init rising exti on ~CS
    RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
    EXTI->RTSR1 |= (1 << spi_pins.cs.pin);
    EXTI->IMR1 |= (1 << spi_pins.cs.pin);
    EXTI->EXTICR[spi_pins.cs.pin / 4] |= ((uint32_t)0x01 << ((spi_pins.cs.pin % 4) * 8));

    NVIC_SetHandler(EXTI4_15_IRQn, exti_irq_handler);
    NVIC_EnableIRQ(EXTI4_15_IRQn);
    NVIC_SetPriority(EXTI4_15_IRQn, 0);

    RCC->APBENR1 |= RCC_APBENR1_SPI2EN;

    reset_and_init_spi();

    NVIC_SetHandler(SPI2_IRQn, spi_irq_handler);
    NVIC_EnableIRQ(SPI2_IRQn);
    NVIC_SetPriority(SPI2_IRQn, 0);
}

// #include "config.h"
// static const gpio_pin_t usart_irq_gpio = { EC_GPIO_UART_INT };


static void spi_irq_handler(void)
{
    if (SPI2->SR & SPI_SR_RXNE) {
        // В прерывание по RXNE попадаем в любом случае
        // Если операция - чтение, принятые байты просто игнорируются

        uint16_t rd = spi_rd_u16();
        if (spi_op == SPI_SLAVE_ADDR_WRITE) {
            uint16_t addr = rd & ~SPI_SLAVE_OPERATION_READ_MASK;
            if (addr >= SPI_SLAVE_USE_PAD_WORDS_SINCE_ADDR) {
                spi_rx_pad_words_cnt = SPI_SLAVE_PAD_WORDS_COUNT;
                spi_tx_pad_words_cnt = SPI_SLAVE_PAD_WORDS_COUNT - 1;
            }
            regmap_ext_prepare_operation(addr);
            if (rd & SPI_SLAVE_OPERATION_READ_MASK) {
                spi_op = SPI_SLAVE_TRANSMIT;
                spi_enable_txe_int();
            } else {
                spi_op = SPI_SLAVE_RECEIVE;
            }
        } else if (spi_op == SPI_SLAVE_RECEIVE) {
            if (spi_rx_pad_words_cnt) {
                spi_rx_pad_words_cnt--;
            } else {
                regmap_ext_write_reg_autoinc(rd);
            }
        }
    }

    if (SPI2->SR & SPI_SR_TXE) {
        // В прерывание по TXE также попадаем в любом случае,
        // даже если бит TXE выключен, в обработчик всё равно попадаем по RXNE
        // Это не мешает, если операция - запись, то в ответ всё равно передаем
        // значения регистров. Мастер может поступить с ними как угодно.

        uint16_t w = 0xAAAA;
        if (spi_tx_pad_words_cnt) {
            w = spi_tx_pad_words_cnt;
            spi_tx_pad_words_cnt--;
        } else {
            w = regmap_ext_read_reg_autoinc();
        }
        spi_tx_u16(w);
        // GPIO_S_TOGGLE(usart_irq_gpio);
    }
}

static void exti_irq_handler(void)
{
    if (EXTI->RPR1 & EXTI_RPR1_RPIF9) {
        EXTI->RPR1 = EXTI_RPR1_RPIF9;

        // По фронту на ~CS выполняется сброс и повторная инициализация SPI
        // Также в regmap снимается флаг занятости
        regmap_ext_end_operation();
        reset_and_init_spi();
    }
}
