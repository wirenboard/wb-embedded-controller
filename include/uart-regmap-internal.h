#pragma once
#include <stdint.h>

#include "wbmcu_system.h"
#include "regmap-int.h"
#include "uart-circ-buffer.h"

struct uart_ctx {
    struct circ_buf_tx circ_buf_tx;
    struct circ_buf_rx circ_buf_rx;

    struct uart_rx rx_data;
    struct uart_ctrl ctrl;
    bool tx_in_progress;
    bool tx_completed;
    bool want_to_tx;
    bool rx_during_tx;
    int tx_bytes_count_in_prev_exchange;
    struct {
        uint16_t pe;
        uint16_t fe;
        uint16_t ne;
        uint16_t ore;
    } errors;
};

struct uart_descr {
    USART_TypeDef *uart;
    int irq_num;
    enum regmap_region ctrl_region;
    enum regmap_region start_tx_region;
    enum regmap_region exchange_region;
    struct uart_ctx *ctx;
};

