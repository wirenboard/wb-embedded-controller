#include "regmap-int.h"
#include "regmap-structs.h"
#include "wbmcu_system.h"
#include "gpio.h"
#include "rcc.h"
#include "array_size.h"
#include "atomic.h"
#include "config.h"
#include <assert.h>
#include <string.h>

/**
 * Модуль реализует мост SPI-UART
 *
 * В regmap есть 3 региона для обмена данными:
 * 1) UART_CTRL - управление UART - сброс, настройка скорости
 * 2) UART_TX_START - начало передачи данных
 * 3) UART_EXCHANGE - обмен данными - одновременно приём из SPI данных на передачу по UART и отправка в SPI накопившихся данных из UART
 *
 * Также используется линия прерывания, чтобы уведомить о готовности к обмену.
 * Обмен данными UART_EXCHANGE - синхронный и должен происходить только после установки прерывания.
 * Запись регионов UART_CTRL и UART_TX_START не требует прерывания.
 *
 * На примере передачи данных через UART работа происходит следующим образом:
 * 1) Сбрасываем порт через UART_CTRL - в функции startup драйвера Linux
 * 2) Настраиваем скорость и другие параметры через UART_CTRL - в функции set_termios
 * 3) Пишем данные в UART_TX_START - в функции start_tx
 *    После записи данных в UART_TX_START начнется передача данных по UART
 * 4) EC взводит прерывание, т.к. готов к обмену - есть место во внутреннем кольцевом буфере
 * 5) Драйвер Linux читает данные из UART_EXCHANGE и записывает (если есть) следующую порцию данных на передачу
 *    Если же данных на передачу нет, то нужно всё равно произвести обмен и записать количество байт - 0
 * 6) После того, как ЕС получит 0 байт на передачу он не будет более взводить прерывание.
 *    Новый цикл передачи нужно будет начинать с пункта 3
 *
 * На примере приёма:
 * 1) Настройка порта - такая же, как выше
 * 2) После прихода данных по UART ЕС взводит прерывание
 * 3) Драйвер Linux читает данные из UART_EXCHANGE и записывает их в буфер
 *    При этом, если в Linux нет данных на передачу, нужно указывать количество байт - 0
 * 4) После того, как прозведен обмен данными, ЕС сбрасывает прерывание, заполняет UART_EXCHANGE и снова взводит прерывание
 * 5) Процесс продолжается до тех пор, пока во внутреннем кольцевом буфере есть ЕС есть данные
 *
 */

static_assert(sizeof(struct uart_rx) == sizeof(struct uart_tx), "Size of uart_rx and uart_tx must be equal");

static const gpio_pin_t usart_tx_gpio = { GPIOA, 2 };
static const gpio_pin_t usart_rx_gpio = { GPIOA, 15 };
static const gpio_pin_t usart_rts_gpio = { GPIOA, 1 };
static const gpio_pin_t usart_irq_gpio = { EC_GPIO_INT };

struct circular_buffer {
    uint8_t data[512];
    uint16_t head;
    uint16_t tail;
};

struct uart_ctx {
    struct circular_buffer tx;
    struct circular_buffer rx;
    int irq_handled;
    bool tx_in_progress;
    // bool ready_for_tx;
    // union uart_exchange exchange;
    struct uart_rx rx_data;
    int tx_bytes_count_in_prev_exchange;
    struct {
        uint16_t pe;
        uint16_t fe;
        uint16_t ne;
        uint16_t ore;
    } errors;
};

static struct uart_ctx uart_ctx = {};

static inline void set_irq_gpio_active(void)
{
    uart_ctx.irq_handled = 1;
    #ifdef EC_GPIO_INT_ACTIVE_HIGH
        GPIO_S_SET(usart_irq_gpio);
    #else
        GPIO_S_RESET(usart_irq_gpio);
    #endif
}

static inline void set_irq_gpio_inactive(void)
{
    uart_ctx.irq_handled = -1;
    #ifdef EC_GPIO_INT_ACTIVE_HIGH
        GPIO_S_RESET(usart_irq_gpio);
    #else
        GPIO_S_SET(usart_irq_gpio);
    #endif
}

static inline void enable_txe_irq(void)
{
    if (!uart_ctx.tx_in_progress) {
        USART2->CR1 |= USART_CR1_TXEIE_TXFNFIE;
        uart_ctx.tx_in_progress = true;
    }
}

static inline void disable_txe_irq(void)
{
    USART2->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
    uart_ctx.tx_in_progress = false;
}

static inline uint16_t get_buffer_used_space(const struct circular_buffer *buf)
{
    return (uint16_t)(buf->head - buf->tail);
}

static inline size_t get_buffer_available_space(const struct circular_buffer *buf)
{
    return sizeof(buf->data) - get_buffer_used_space(buf);
}

static inline void put_byte_to_buffer(struct circular_buffer *buf, uint8_t byte)
{
    uint16_t byte_pos = buf->head % sizeof(buf->data);
    buf->head++;
    buf->data[byte_pos] = byte;
}

static inline uint8_t push_byte_from_buffer(struct circular_buffer *buf)
{
    uint16_t byte_pos = buf->tail % sizeof(buf->data);
    uint16_t byte = buf->data[byte_pos];
    buf->tail++;
    return byte;
}

static void uart_put_tx_data_from_regmap_to_buffer(const struct uart_tx *tx)
{
    if ((uart_ctx.rx_data.ready_for_tx) && (tx->bytes_to_send_count > 0)) {
        disable_txe_irq();
        for (size_t i = 0; i < tx->bytes_to_send_count; i++) {
            put_byte_to_buffer(&uart_ctx.tx, tx->bytes_to_send[i]);
        }
        uart_ctx.rx_data.ready_for_tx = false;
        enable_txe_irq();
    }

    uart_ctx.tx_bytes_count_in_prev_exchange = tx->bytes_to_send_count;
}

static void uart_irq_handler(void)
{
    if (USART2->ISR & USART_ISR_RXNE_RXFNE) {
        uint8_t byte = USART2->RDR;
        if (get_buffer_available_space(&uart_ctx.rx) > 0) {
            put_byte_to_buffer(&uart_ctx.rx, byte);
        }
    }

    if (USART2->ISR & USART_ISR_TXE_TXFNF) {
        if (get_buffer_used_space(&uart_ctx.tx) > 0) {
            USART2->TDR = push_byte_from_buffer(&uart_ctx.tx);
        } else {
            disable_txe_irq();
        }
    }

    if (USART2->ISR & USART_ISR_PE) {
        uart_ctx.errors.pe++;
        USART2->ICR |= USART_ICR_PECF;
    }
    if (USART2->ISR & USART_ISR_FE) {
        uart_ctx.errors.fe++;
        USART2->ICR |= USART_ICR_FECF;
    }
    if (USART2->ISR & USART_ISR_NE) {
        uart_ctx.errors.ne++;
        USART2->ICR |= USART_ICR_NECF;
    }
    if (USART2->ISR & USART_ISR_ORE) {
        uart_ctx.errors.ore++;
        USART2->ICR |= USART_ICR_ORECF;
    }
}

void uart_regmap_init(void)
{
    uart_ctx.tx.head = 0;
    uart_ctx.tx.tail = 0;
    uart_ctx.rx.head = 0;
    uart_ctx.rx.tail = 0;

    // Init GPIO
    GPIO_S_SET_OUTPUT(usart_tx_gpio);
    GPIO_S_SET_OUTPUT(usart_rts_gpio);
    GPIO_S_SET_INPUT(usart_rx_gpio);
    GPIO_S_SET_AF(usart_tx_gpio, 1);
    GPIO_S_SET_AF(usart_rts_gpio, 1);
    GPIO_S_SET_AF(usart_rx_gpio, 1);

    GPIO_S_SET_PUSHPULL(usart_tx_gpio);
    GPIO_S_SET_OUTPUT(usart_irq_gpio);

    RCC->APBENR1 |= RCC_APBENR1_USART2EN;

    // Reset USART
    RCC->APBRSTR1 |= RCC_APBRSTR1_USART2RST;
    RCC->APBRSTR1 &= ~RCC_APBRSTR1_USART2RST;

    // Init USART
    USART2->BRR = SystemCoreClock / 115200;
    USART2->CR1 |= 0x08 << 21 | 0x08 << 16;     // driver enable assert and de-assert time;
    USART2->CR3 |= USART_CR3_DEM;               // activate external transceiver control through the DE (Driver Enable) signal
    USART2->CR3 |= USART_CR3_EIE;               // error interrupt enable
    USART2->CR1 |= USART_CR1_TE | USART_CR1_UE | USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE | USART_CR1_PEIE;

    NVIC_EnableIRQ(USART2_IRQn);
    NVIC_SetPriority(USART2_IRQn, 1);
    NVIC_SetHandler(USART2_IRQn, uart_irq_handler);
}

void uart_regmap_do_periodic_work(void)
{
    if (regmap_is_region_changed(REGMAP_REGION_UART_CTRL)) {
        struct REGMAP_UART_CTRL uart_ctrl = {};
        if (regmap_get_region_data(REGMAP_REGION_UART_CTRL, &uart_ctrl, sizeof(uart_ctrl))) {
            if (uart_ctrl.reset) {
                uart_ctrl.reset = 0;

                memset(&uart_ctx, 0, sizeof(uart_ctx));

                disable_txe_irq();
                set_irq_gpio_inactive();
                regmap_clear_changed(REGMAP_REGION_UART_EXCHANGE);
            }

            regmap_clear_changed(REGMAP_REGION_UART_CTRL);
        }
    }

    if (regmap_is_region_changed(REGMAP_REGION_UART_TX_START)) {
        struct REGMAP_UART_TX_START uart_tx_start;
        if (regmap_get_region_data(REGMAP_REGION_UART_TX_START, &uart_tx_start, sizeof(uart_tx_start))) {
            uart_put_tx_data_from_regmap_to_buffer(&uart_tx_start.tx_start);
            regmap_clear_changed(REGMAP_REGION_UART_TX_START);
        }
    }

    if (regmap_is_region_changed(REGMAP_REGION_UART_EXCHANGE)) {
        // Это означает, что TX записали, а из RX всё прочитали за одну транзакцию
        union uart_exchange uart_exchange;
        if (regmap_get_region_data(REGMAP_REGION_UART_EXCHANGE, &uart_exchange, sizeof(uart_exchange))) {

            uart_put_tx_data_from_regmap_to_buffer(&uart_exchange.tx);

            uart_ctx.rx_data.read_bytes_count = 0;

            set_irq_gpio_inactive();
            regmap_clear_changed(REGMAP_REGION_UART_EXCHANGE);
        }
    }

    if (uart_ctx.irq_handled < 0) {
        uart_ctx.irq_handled++;
    } else if (uart_ctx.irq_handled == 0) {
        if (get_buffer_available_space(&uart_ctx.tx) >= UART_REGMAP_BUFFER_SIZE) {
            uart_ctx.rx_data.ready_for_tx = 1;
        }

        USART2->CR1 &= ~USART_CR1_RXNEIE_RXFNEIE;
        while ((get_buffer_used_space(&uart_ctx.rx) > 0) &&
               (uart_ctx.rx_data.read_bytes_count < UART_REGMAP_BUFFER_SIZE))
        {
            uart_ctx.rx_data.read_bytes[uart_ctx.rx_data.read_bytes_count] = push_byte_from_buffer(&uart_ctx.rx);
            uart_ctx.rx_data.read_bytes_count++;
        }
        USART2->CR1 |= USART_CR1_RXNEIE_RXFNEIE;

        bool tx_irq_needed = false;
        bool rx_irq_needed = false;

        if (uart_ctx.rx_data.read_bytes_count > 0) {
            rx_irq_needed = true;
        }

        if (uart_ctx.rx_data.ready_for_tx) {
            if (uart_ctx.tx_bytes_count_in_prev_exchange > 0) {
                tx_irq_needed = true;
            }
        }

        if (regmap_set_region_data(REGMAP_REGION_UART_EXCHANGE, &uart_ctx.rx_data, sizeof(struct uart_rx))) {
            if (tx_irq_needed || rx_irq_needed) {
                set_irq_gpio_active();
            }
        }
    }
}
