#include "config.h"

#if defined EC_UART_REGMAP_SUPPORT

#include "uart-regmap.h"
#include "wbmcu_system.h"
#include "rcc.h"
#include "array_size.h"
#include "atomic.h"
#include "bits.h"
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

#define UART_RX_BYTE_ERROR_PE               BIT(0)
#define UART_RX_BYTE_ERROR_FE               BIT(1)
#define UART_RX_BYTE_ERROR_NE               BIT(2)
#define UART_RX_BYTE_ERROR_ORE              BIT(3)

static_assert(sizeof(struct uart_rx) == sizeof(struct uart_tx), "Size of uart_rx and uart_tx must be equal");


static inline void enable_txe_irq(const struct uart_descr *u)
{
    if (!u->ctx->tx_in_progress) {
        ATOMIC {
            uint32_t val = u->uart->CR1;
            if ((u->ctx->ctrl.rs485_enabled) && (!u->ctx->rx_during_tx)) {
                val &= ~USART_CR1_RE;
            }
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
            circ_buffer_tx_push(&u->ctx->circ_buf_tx, tx->bytes_to_send[i]);
        }
        u->ctx->rx_data.ready_for_tx = false;
        enable_txe_irq(u);
    }

    u->ctx->tx_bytes_count_in_prev_exchange = tx->bytes_to_send_count;
}

void uart_apply_ctrl(const struct uart_descr *u, bool enable_req)
{
    struct uart_ctx *ctx = u->ctx;
    struct uart_ctrl *ctrl = &ctx->ctrl;

    if (ctrl->enable) {
        while (u->uart->ISR & USART_ISR_BUSY) {};
    }

    NVIC_DisableIRQ(u->irq_num);
    u->uart->CR1 &= ~USART_CR1_UE;
    NVIC_ClearPendingIRQ(u->irq_num);

    // baud rate
    u->uart->BRR = SystemCoreClock / (ctrl->baud_x100 * 100);

    // stop bits
    u->uart->CR2 &= ~USART_CR2_STOP;
    u->uart->CR2 |= (ctrl->stop_bits << USART_CR2_STOP_Pos);

    // parity
    switch (ctrl->parity) {
    default:
    case UART_PARITY_NONE:
        u->uart->CR1 &= ~(USART_CR1_PCE | USART_CR1_PS | USART_CR1_M);
        break;

    case UART_PARITY_EVEN:
        u->uart->CR1 &= ~USART_CR1_PS;
        u->uart->CR1 |= USART_CR1_PCE | USART_CR1_M0;
        break;

    case UART_PARITY_ODD:
        u->uart->CR1 |= USART_CR1_PCE | USART_CR1_PS | USART_CR1_M0;
        break;
    }

    if (ctrl->rs485_enabled) {
        u->uart->CR1 |= 0x08 << 21 | 0x08 << 16;     // driver enable assert and de-assert time;
        u->uart->CR3 |= USART_CR3_DEM;               // activate external transceiver control through the DE (Driver Enable) signal

        if (ctrl->rs485_rx_during_tx) {
            u->ctx->rx_during_tx = true;
        } else {
            u->ctx->rx_during_tx = false;
        }
    } else {
        u->uart->CR3 &= ~USART_CR3_DEM;
        u->ctx->rx_during_tx = true;
    }

    u->uart->CR1 |= USART_CR1_TE | USART_CR1_UE | USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE | USART_CR1_TCIE;

    if ((ctrl->enable == 0) && (enable_req == 1)) {
        circ_buffer_reset(&u->ctx->circ_buf_tx.i);
        circ_buffer_reset(&u->ctx->circ_buf_rx.i);

        u->ctx->rx_buf_overflow = false;

        ctrl->enable = 1;
    }

    if ((ctrl->enable == 1) && (enable_req == 0)) {

        ctrl->enable = 0;
    }

    if (ctrl->enable) {
        u->uart->CR1 |= USART_CR1_UE;
        u->uart->ICR = USART_ICR_TCCF;
        NVIC_EnableIRQ(u->irq_num);
    }
}

void uart_regmap_process_irq(const struct uart_descr *u)
{
    struct uart_ctx *ctx = u->ctx;

    if (u->uart->ISR & USART_ISR_TC) {
        u->uart->ICR = USART_ICR_TCCF;
        if ((u->ctx->ctrl.rs485_enabled) && (!u->ctx->rx_during_tx)) {
            u->uart->CR1 |= USART_CR1_RE;
        }
        ctx->tx_completed = true;
    }

    if (u->uart->ISR & USART_ISR_RXNE_RXFNE) {
        uint8_t byte = u->uart->RDR;
        uint8_t err_flags = 0;

        if (u->uart->ISR & USART_ISR_PE) {
            u->uart->ICR = USART_ICR_PECF;
            err_flags |= UART_RX_BYTE_ERROR_PE;
            ctx->errors.pe++;
        }
        if (u->uart->ISR & USART_ISR_FE) {
            u->uart->ICR = USART_ICR_FECF;
            err_flags |= UART_RX_BYTE_ERROR_FE;
            ctx->errors.fe++;
        }
        if (u->uart->ISR & USART_ISR_NE) {
            u->uart->ICR = USART_ICR_NECF;
            err_flags |= UART_RX_BYTE_ERROR_NE;
            ctx->errors.ne++;
        }
        if (u->uart->ISR & USART_ISR_ORE) {
            u->uart->ICR = USART_ICR_ORECF;
            err_flags |= UART_RX_BYTE_ERROR_ORE;
            ctx->errors.ore++;
        }
        if (ctx->rx_buf_overflow) {
            ctx->rx_buf_overflow = false;
            err_flags |= UART_RX_BYTE_ERROR_ORE;
        }

        if (circ_buffer_get_available_space(&ctx->circ_buf_rx.i) > 0) {
            circ_buffer_rx_push(&ctx->circ_buf_rx, byte, err_flags);
        } else {
            ctx->rx_buf_overflow = true;
        }
    }

    if (u->uart->ISR & USART_ISR_TXE_TXFNF) {
        if (circ_buffer_get_used_space(&ctx->circ_buf_tx.i) > 0) {
            // also clears TXFNF flag
            u->uart->TDR = circ_buffer_tx_pop(&ctx->circ_buf_tx);
        } else {
            u->uart->ICR = USART_ICR_TXFECF;
            disable_txe_irq(u);
        }
    }
}

void uart_regmap_process_ctrl(const struct uart_descr *u, const struct uart_ctrl *ctrl)
{
    struct uart_ctx *ctx = u->ctx;

    // valid baud 1200..115200
    if ((ctrl->baud_x100 >= 12) && (ctrl->baud_x100 <= 1152)) {
        ctx->ctrl.baud_x100 = ctrl->baud_x100;
    }

    if ((ctrl->parity == UART_PARITY_NONE) ||
        (ctrl->parity == UART_PARITY_EVEN) ||
        (ctrl->parity == UART_PARITY_ODD))
    {
        ctx->ctrl.parity = ctrl->parity;
    }

    // all values are valid
    ctx->ctrl.stop_bits = ctrl->stop_bits;
    ctx->ctrl.rs485_enabled = ctrl->rs485_enabled;
    ctx->ctrl.rs485_rx_during_tx = ctrl->rs485_rx_during_tx;

    bool enable_req = false;
    if (ctrl->enable) {
        enable_req = true;
    }

    uart_apply_ctrl(u, enable_req);

    u->ctx->ctrl.ctrl_applyed = 1;
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

    // if (ctx->ctrl.enable == 0) {
    //     return;
    // }

    if (circ_buffer_get_available_space(&ctx->circ_buf_tx.i) >= UART_REGMAP_BUFFER_SIZE) {
        ctx->rx_data.ready_for_tx = 1;
    }

    if (ctx->tx_completed) {
        ctx->tx_completed = false;
        ctx->rx_data.tx_completed = 1;
    }

    ATOMIC {
        u->uart->CR1 &= ~USART_CR1_RXNEIE_RXFNEIE;
    }

    if ((circ_buffer_get_used_space(&ctx->circ_buf_rx.i) > 0) &&
        (ctx->rx_data.read_bytes_count == 0))
    {
        // Первый байт определяет формат данных - с ошибками или без
        uint8_t byte, err_flags;
        circ_buffer_rx_pop(&ctx->circ_buf_rx, &byte, &err_flags);
        if (err_flags) {
            ctx->rx_data.data_format = 1;
            ctx->rx_data.bytes_with_errors[0].byte = byte;
            ctx->rx_data.bytes_with_errors[0].err_flags = err_flags;
        } else {
            ctx->rx_data.data_format = 0;
            ctx->rx_data.read_bytes[0] = byte;
        }
        ctx->rx_data.read_bytes_count = 1;
    }

    uint8_t regmap_buf_size = UART_REGMAP_BUFFER_SIZE;
    if (ctx->rx_data.data_format == 1) {
        regmap_buf_size = ARRAY_SIZE(ctx->rx_data.bytes_with_errors);
    }

    while ((circ_buffer_get_used_space(&ctx->circ_buf_rx.i) > 0) &&
            (ctx->rx_data.read_bytes_count < regmap_buf_size))
    {
        uint8_t byte, err_flags;
        // Нельзя сразу извлекать байт из буфера, т.к. он может не подойти по формату
        circ_buffer_rx_get(&ctx->circ_buf_rx, &byte, &err_flags);
        if (ctx->rx_data.data_format == 1) {
            ctx->rx_data.bytes_with_errors[ctx->rx_data.read_bytes_count].byte = byte;
            ctx->rx_data.bytes_with_errors[ctx->rx_data.read_bytes_count].err_flags = err_flags;
        } else {
            if (err_flags) {
                // Если встретили байт с ошибкой, а текущий формат данных - без ошибок,
                // то прекращаем заполнять буфер в regmap.
                // При следующем обмене данные будут в формате с ошибками
                break;
            }
            ctx->rx_data.read_bytes[ctx->rx_data.read_bytes_count] = byte;
        }
        ctx->rx_data.read_bytes_count++;
        // Байт в итоге подходит по формату, извлекаем его из буфера
        circ_buffer_tail_inc(&ctx->circ_buf_rx.i);
    }
    ATOMIC {
        u->uart->CR1 |= USART_CR1_RXNEIE_RXFNEIE;
    }
}

bool uart_regmap_is_irq_needed(const struct uart_descr *u)
{
    struct uart_ctx *ctx = u->ctx;

    if (ctx->ctrl.enable == 0) {
        return false;
    }

    if (ctx->rx_data.read_bytes_count > 0) {
        return true;
    }

    if (ctx->rx_data.ready_for_tx) {
        if (ctx->tx_bytes_count_in_prev_exchange > 0) {
            return true;
        }
    }

    if (ctx->rx_data.tx_completed) {
        return true;
    }

    if (ctx->want_to_tx) {
        return true;
    }

    return false;
}

#endif
