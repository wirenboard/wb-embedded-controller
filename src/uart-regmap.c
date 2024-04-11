#include "regmap-int.h"
#include "regmap-structs.h"
#include "wbmcu_system.h"
#include "gpio.h"
#include "rcc.h"
#include "array_size.h"
#include "atomic.h"
#include "config.h"

static const gpio_pin_t usart_tx_gpio = { GPIOA, 2 };
static const gpio_pin_t usart_rx_gpio = { GPIOA, 15 };
static const gpio_pin_t usart_rts_gpio = { GPIOA, 1 };
static const gpio_pin_t usart_irq_gpio = { EC_GPIO_INT };

struct circular_buffer {
    uint8_t data[128];
    uint8_t head;
    uint8_t tail;
};

struct uart_ctx {
    struct circular_buffer tx;
    struct circular_buffer rx;
    bool rx_irq_handled;
};

static struct uart_ctx uart_ctx = {};

static inline void set_irq_gpio_active(void)
{
    uart_ctx.rx_irq_handled = true;
    #ifdef EC_GPIO_INT_ACTIVE_HIGH
        GPIO_S_SET(usart_irq_gpio);
    #else
        GPIO_S_RESET(usart_irq_gpio);
    #endif
}

static inline void set_irq_gpio_inactive(void)
{
    uart_ctx.rx_irq_handled = false;
    #ifdef EC_GPIO_INT_ACTIVE_HIGH
        GPIO_S_RESET(usart_irq_gpio);
    #else
        GPIO_S_SET(usart_irq_gpio);
    #endif
}

static inline uint8_t get_buffer_used_space(const struct circular_buffer *buf)
{
    return (uint8_t)(buf->head - buf->tail);
}

static inline size_t get_buffer_available_space(const struct circular_buffer *buf)
{
    return sizeof(buf->data) - get_buffer_used_space(buf);
}

static inline void put_byte_to_buffer(struct circular_buffer *buf, uint8_t byte)
{
    uint8_t byte_pos = buf->head % sizeof(buf->data);
    buf->head++;
    buf->data[byte_pos] = byte;
}

static inline uint8_t push_byte_from_buffer(struct circular_buffer *buf)
{
    uint8_t byte_pos = buf->tail % sizeof(buf->data);
    uint8_t byte = buf->data[byte_pos];
    buf->tail++;
    return byte;
}

static inline uint8_t get_byte_from_buffer(const struct circular_buffer *buf, uint8_t index)
{
    uint8_t byte_pos = (buf->tail + index) % sizeof(buf->data);
    return buf->data[byte_pos];
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
    USART2->CR1 |= USART_CR1_TE | USART_CR1_UE | USART_CR1_RE;

    // Add test string to tx buffer
    const char test_string[] = "Hello, World!\r\n";
    for (size_t i = 0; i < ARRAY_SIZE(test_string); i++) {
        put_byte_to_buffer(&uart_ctx.tx, test_string[i]);
    }
}

void uart_regmap_do_periodic_work(void)
{
    static struct REGMAP_UART_TX uart_tx = {};

    if (regmap_is_region_changed(REGMAP_REGION_UART_TX)) {
        regmap_get_region_data(REGMAP_REGION_UART_TX, &uart_tx, sizeof(uart_tx));

        uint16_t regmap_bytes_count = uart_tx.bytes_to_send_count;
        uint16_t buffer_free_space = get_buffer_available_space(&uart_ctx.tx);

        if (regmap_bytes_count > buffer_free_space) {
            regmap_bytes_count = buffer_free_space;
        }
        for (size_t i = 0; i < regmap_bytes_count; i++) {
            put_byte_to_buffer(&uart_ctx.tx, uart_tx.bytes_to_send[i]);
        }

        struct REGMAP_UART_TX_INFO uart_tx_info = {};
        uart_tx_info.number_of_send_bytes = regmap_bytes_count;
        regmap_set_region_data(REGMAP_REGION_UART_TX_INFO, &uart_tx_info, sizeof(uart_tx_info));

        regmap_clear_changed(REGMAP_REGION_UART_TX);
    }

    if (uart_ctx.rx_irq_handled) {
        if (regmap_is_region_was_read(REGMAP_REGION_UART_RX)) {
            set_irq_gpio_inactive();
            regmap_clear_was_read(REGMAP_REGION_UART_RX);
        }
    } else {
        uint16_t rx_bytes_count = get_buffer_used_space(&uart_ctx.rx);
        if (rx_bytes_count > 0) {
            struct REGMAP_UART_RX uart_rx;
            if (rx_bytes_count > ARRAY_SIZE(uart_rx.read_bytes)) {
                rx_bytes_count = ARRAY_SIZE(uart_rx.read_bytes);
            }
            uart_rx.read_bytes_count = rx_bytes_count;
            for (size_t i = 0; i < rx_bytes_count; i++) {
                uart_rx.read_bytes[i] = push_byte_from_buffer(&uart_ctx.rx);
            }
            regmap_set_region_data(REGMAP_REGION_UART_RX, &uart_rx, sizeof(uart_rx));
            set_irq_gpio_active();
        }
    }

    if (USART2->ISR & USART_ISR_TXE_TXFNF) {
        if (uart_ctx.tx.head != uart_ctx.tx.tail) {
            USART2->TDR = push_byte_from_buffer(&uart_ctx.tx);
        }
    }

    if (USART2->ISR & USART_ISR_RXNE_RXFNE) {
        uint8_t byte = USART2->RDR;
        if (get_buffer_available_space(&uart_ctx.rx) > 0) {
            put_byte_to_buffer(&uart_ctx.rx, byte);
        }
    }
}
