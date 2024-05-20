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
    uint8_t data[512];
    uint16_t head;
    uint16_t tail;
};

struct uart_ctx {
    struct circular_buffer tx;
    struct circular_buffer rx;
    int irq_handled;
    bool tx_in_progress;
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
    uart_ctx.irq_handled = -10;
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

    // Add test string to tx buffer
    const char test_string[] = "Hello, World!\r\n";
    for (size_t i = 0; i < ARRAY_SIZE(test_string); i++) {
        put_byte_to_buffer(&uart_ctx.tx, test_string[i]);
    }
}

void uart_regmap_do_periodic_work(void)
{
    static struct REGMAP_UART_TX uart_tx = {};
    static struct REGMAP_UART_RX uart_rx = {};
    static bool want_to_tx = false;

    if (regmap_is_region_changed(REGMAP_REGION_UART_CTRL)) {
        struct REGMAP_UART_CTRL uart_ctrl = {};
        regmap_get_region_data(REGMAP_REGION_UART_CTRL, &uart_ctrl, sizeof(uart_ctrl));

        if (uart_ctrl.reset) {
            uart_ctx.tx.head = 0;
            uart_ctx.tx.tail = 0;
            uart_ctx.rx.head = 0;
            uart_ctx.rx.tail = 0;
            uart_ctx.irq_handled = 0;
            uart_ctx.errors.pe = 0;
            uart_ctx.errors.fe = 0;
            uart_ctx.errors.ne = 0;
            uart_ctx.errors.ore = 0;
            uart_ctrl.reset = 0;

            disable_txe_irq();

            want_to_tx = false;

            uart_rx.ready_for_tx = 0;
            uart_rx.read_bytes_count = 0;

            uart_tx.bytes_to_send_count = 0;

            set_irq_gpio_inactive();
            regmap_clear_changed(REGMAP_REGION_UART_TX);
        }

        regmap_clear_changed(REGMAP_REGION_UART_CTRL);
    }

    if (regmap_is_region_changed(REGMAP_REGION_UART_TX_START)) {
        struct REGMAP_UART_TX_START uart_tx_start = {};
        regmap_get_region_data(REGMAP_REGION_UART_TX_START, &uart_tx_start, sizeof(uart_tx_start));

        if (uart_tx_start.bytes_to_send_count > 0) {
            for (size_t i = 0; i < uart_tx_start.bytes_to_send_count; i++) {
                put_byte_to_buffer(&uart_ctx.tx, uart_tx_start.bytes_to_send[i]);
            }
            want_to_tx = true;
            enable_txe_irq();
        }

        regmap_clear_changed(REGMAP_REGION_UART_TX_START);
    }

    if (regmap_is_region_changed(REGMAP_REGION_UART_TX)) {
        // Это означает, что TX записали, а из RX всё прочитали за одну транзакцию
        regmap_get_region_data(REGMAP_REGION_UART_TX, &uart_tx, sizeof(uart_tx));

        if (uart_rx.ready_for_tx && uart_tx.bytes_to_send_count > 0) {
            for (size_t i = 0; i < uart_tx.bytes_to_send_count; i++) {
                put_byte_to_buffer(&uart_ctx.tx, uart_tx.bytes_to_send[i]);
            }
            enable_txe_irq();
        }

        uart_rx.ready_for_tx = 0;
        uart_rx.read_bytes_count = 0;

        set_irq_gpio_inactive();
        regmap_clear_changed(REGMAP_REGION_UART_TX);
    }

    if (uart_ctx.irq_handled < 0) {
        uart_ctx.irq_handled++;
    } else if (uart_ctx.irq_handled == 0) {
        // Готовы к следующему прерыванию, нужно заполнить регмап
        bool tx_irq_needed = false;
        bool rx_irq_needed = false;

        if (get_buffer_available_space(&uart_ctx.tx) >= sizeof(uart_tx.bytes_to_send)) {
            uart_rx.ready_for_tx = 1;
        }

        while (get_buffer_used_space(&uart_ctx.rx) > 0 && uart_rx.read_bytes_count < ARRAY_SIZE(uart_rx.read_bytes)) {
            uart_rx.read_bytes[uart_rx.read_bytes_count] = push_byte_from_buffer(&uart_ctx.rx);
            uart_rx.read_bytes_count++;
        }

        if (uart_rx.read_bytes_count > 0) {
            rx_irq_needed = true;
        }

        if (uart_rx.ready_for_tx && ((uart_tx.bytes_to_send_count > 0) || (want_to_tx))) {
            want_to_tx = false;
            tx_irq_needed = true;
        }

        if (regmap_set_region_data(REGMAP_REGION_UART_RX, &uart_rx, sizeof(uart_rx))) {
            if (tx_irq_needed || rx_irq_needed) {
                set_irq_gpio_active();
            }
        }
    }
}
