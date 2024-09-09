#include "config.h"

#if defined EC_UART_REGMAP_SUPPORT

#include "uart-regmap.h"
#include "wbmcu_system.h"
#include "rcc.h"
#include "array_size.h"
#include "atomic.h"
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


static inline void enable_txe_irq(const struct uart_descr *u)
{
    if (!u->ctx->tx_in_progress) {
        ATOMIC {
            uint32_t val = u->uart->CR1;
            val &= ~USART_CR1_RE;
            val |= USART_CR1_TXEIE_TXFNFIE;
            u->uart->CR1 = val;
            u->ctx->tx_in_progress = true;
        }
    }
}

static inline void disable_txe_irq(const struct uart_descr *u)
{
    u->uart->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
    u->ctx->tx_in_progress = false;
}

static void uart_put_tx_data_from_regmap_to_circ_buffer(const struct uart_descr *u, const struct uart_tx *tx)
{
    if ((u->ctx->rx_data.ready_for_tx) && (tx->bytes_to_send_count > 0)) {
        disable_txe_irq(u);
        for (size_t i = 0; i < tx->bytes_to_send_count; i++) {
            circ_buffer_push(&u->ctx->circ_buf_tx, tx->bytes_to_send[i]);
        }
        u->ctx->rx_data.ready_for_tx = false;
        enable_txe_irq(u);
    }

    u->ctx->tx_bytes_count_in_prev_exchange = tx->bytes_to_send_count;
}

static void uart_set_baud(const struct uart_descr *u, uint16_t baud_x100)
{
    NVIC_DisableIRQ(u->irq_num);
    u->uart->CR1 &= ~USART_CR1_UE;
    NVIC_ClearPendingIRQ(u->irq_num);

    u->uart->BRR = SystemCoreClock / (baud_x100 * 100);

    u->uart->CR1 |= USART_CR1_UE;
    u->ctx->ctrl.baud_x100 = baud_x100;
    u->uart->ICR = USART_ICR_TCCF;
    NVIC_EnableIRQ(u->irq_num);
}

static void uart_set_parity(const struct uart_descr *u, enum uart_parity parity)
{
    NVIC_DisableIRQ(u->irq_num);
    while (u->uart->ISR & USART_ISR_BUSY) {};
    u->uart->CR1 &= ~USART_CR1_UE;
    NVIC_ClearPendingIRQ(u->irq_num);

    switch (parity) {
    case UART_PARITY_NONE:
        u->uart->CR1 &= ~(USART_CR1_PCE | USART_CR1_PS | USART_CR1_M);
        break;
    case UART_PARITY_EVEN:
        u->uart->CR1 &= ~USART_CR1_PS;
        u->uart->CR1 |= USART_CR1_PCE | USART_CR1_M;
        break;
    case UART_PARITY_ODD:
        u->uart->CR1 |= USART_CR1_PCE | USART_CR1_PS | USART_CR1_M;
        break;
    default:
        break;
    }

    u->ctx->ctrl.parity = parity;
    u->uart->CR1 |= USART_CR1_UE;
    u->uart->ICR = USART_ICR_TCCF;
    NVIC_EnableIRQ(u->irq_num);
}

static void uart_set_stop_bits(const struct uart_descr *u, enum uart_stop_bits stop_bits)
{
    NVIC_DisableIRQ(u->irq_num);
    while (u->uart->ISR & USART_ISR_BUSY) {};
    u->uart->CR1 &= ~USART_CR1_UE;
    NVIC_ClearPendingIRQ(u->irq_num);

    u->uart->CR2 &= ~USART_CR2_STOP;
    u->uart->CR2 |= (stop_bits << USART_CR2_STOP_Pos);

    u->ctx->ctrl.stop_bits = stop_bits;

    u->uart->CR1 |= USART_CR1_UE;
    u->uart->ICR = USART_ICR_TCCF;
    NVIC_EnableIRQ(u->irq_num);
}

void uart_regmap_process_irq(const struct uart_descr *u)
{
    struct uart_ctx *ctx = u->ctx;

    if (u->uart->ISR & USART_ISR_TC) {
        u->uart->ICR = USART_ICR_TCCF;
        u->uart->CR1 |= USART_CR1_RE;
        ctx->tx_completed = true;
    }

    if (u->uart->ISR & USART_ISR_RXNE_RXFNE) {
        uint8_t byte = u->uart->RDR;
        if (cicr_buffer_get_available_space(&ctx->circ_buf_rx) > 0) {
            circ_buffer_push(&ctx->circ_buf_rx, byte);
        }
    }

    if (u->uart->ISR & USART_ISR_TXE_TXFNF) {
        if (circ_buffer_get_used_space(&ctx->circ_buf_tx) > 0) {
            // also clears TXFNF flag
            u->uart->TDR = circ_buffer_pop(&ctx->circ_buf_tx);
        } else {
            u->uart->ICR = USART_ICR_TXFECF;
            disable_txe_irq(u);
        }
    }

    if (u->uart->ISR & USART_ISR_PE) {
        ctx->errors.pe++;
        u->uart->ICR = USART_ICR_PECF;
    }
    if (u->uart->ISR & USART_ISR_FE) {
        ctx->errors.fe++;
        u->uart->ICR = USART_ICR_FECF;
    }
    if (u->uart->ISR & USART_ISR_NE) {
        ctx->errors.ne++;
        u->uart->ICR = USART_ICR_NECF;
    }
    if (u->uart->ISR & USART_ISR_ORE) {
        ctx->errors.ore++;
        u->uart->ICR = USART_ICR_ORECF;
    }
}

void uart_regmap_process_ctrl(const struct uart_descr *u, const struct uart_ctrl *ctrl)
{
    struct uart_ctx *ctx = u->ctx;

    if ((ctx->ctrl.enable == 0) && (ctrl->enable == 1)) {
        u->uart_hw_init();

        circ_buffer_reset(&ctx->circ_buf_tx);
        circ_buffer_reset(&ctx->circ_buf_rx);

        u->uart->BRR = SystemCoreClock / 115200;
        u->uart->CR1 |= 0x08 << 21 | 0x08 << 16;     // driver enable assert and de-assert time;
        u->uart->CR3 |= USART_CR3_DEM;               // activate external transceiver control through the DE (Driver Enable) signal
        u->uart->CR3 |= USART_CR3_EIE;               // error interrupt enable
        u->uart->CR1 |= USART_CR1_TE | USART_CR1_UE | USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE | USART_CR1_PEIE | USART_CR1_TCIE;

        u->uart->ICR = USART_ICR_TCCF;

        NVIC_EnableIRQ(u->irq_num);
        NVIC_SetPriority(u->irq_num, 1);

        uart_set_baud(u, 1152);
        uart_set_parity(u, UART_PARITY_NONE);
        uart_set_stop_bits(u, UART_STOP_BITS_1);


        ctx->ctrl.enable = 1;
        ctx->rx_data.ready_for_tx = 1;
    }

    if ((ctx->ctrl.enable == 1) && (ctrl->enable == 0)) {
        u->uart_hw_deinit();
        NVIC_DisableIRQ(u->irq_num);

        memset(ctx, 0, sizeof(*ctx));
    }

    uint16_t baud = ctrl->baud_x100;
    if ((baud < 12) || (baud > 1152)) {
        baud = 1152;
    }

    if (ctx->ctrl.baud_x100 != baud) {
        uart_set_baud(u, baud);
    }

    if (ctx->ctrl.parity != ctrl->parity) {
        uart_set_parity(u, ctrl->parity);
    }

    if (ctx->ctrl.stop_bits != ctrl->stop_bits) {
        uart_set_stop_bits(u, ctrl->stop_bits);
    }

    ctx->ctrl.ctrl_applyed = 1;
}

void uart_regmap_process_start_tx(const struct uart_descr *u, const struct uart_tx *tx)
{
    uart_put_tx_data_from_regmap_to_circ_buffer(u, tx);
}


void uart_regmap_process_exchange(const struct uart_descr *u, union uart_exchange *e)
{
    // Это означает, что TX записали, а из RX всё прочитали за одну транзакцию
    uart_put_tx_data_from_regmap_to_circ_buffer(u, &e->tx);

    u->ctx->rx_data.read_bytes_count = 0;
    u->ctx->rx_data.tx_completed = 0;
}

void uart_regmap_collect_data_for_new_exchange(const struct uart_descr *u)
{
    struct uart_ctx *ctx = u->ctx;

    if (cicr_buffer_get_available_space(&ctx->circ_buf_tx) >= UART_REGMAP_BUFFER_SIZE) {
        ctx->rx_data.ready_for_tx = 1;
    }

    if (ctx->tx_completed) {
        ctx->tx_completed = false;
        ctx->rx_data.tx_completed = 1;
    }

    ATOMIC {
        u->uart->CR1 &= ~USART_CR1_RXNEIE_RXFNEIE;
    }
    while ((circ_buffer_get_used_space(&ctx->circ_buf_rx) > 0) &&
            (ctx->rx_data.read_bytes_count < UART_REGMAP_BUFFER_SIZE))
    {
        ctx->rx_data.read_bytes[ctx->rx_data.read_bytes_count] = circ_buffer_pop(&ctx->circ_buf_rx);
        ctx->rx_data.read_bytes_count++;
    }
    ATOMIC {
        u->uart->CR1 |= USART_CR1_RXNEIE_RXFNEIE;
    }
}

bool uart_regmap_is_irq_needed(const struct uart_descr *u)
{
    struct uart_ctx *ctx = u->ctx;

    bool tx_irq_needed = false;
    bool rx_irq_needed = false;

    if (ctx->rx_data.read_bytes_count > 0) {
        rx_irq_needed = true;
    }

    if (ctx->rx_data.ready_for_tx) {
        if (ctx->tx_bytes_count_in_prev_exchange > 0) {
            tx_irq_needed = true;
        }
    }

    if (ctx->rx_data.tx_completed) {
        tx_irq_needed = true;
    }

    if (ctx->want_to_tx) {
        tx_irq_needed = true;
    }

    return tx_irq_needed || rx_irq_needed;
}

#endif
