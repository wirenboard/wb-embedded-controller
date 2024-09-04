#pragma once
#include <stdint.h>

#include "wbmcu_system.h"
#include "regmap-int.h"
#include "uart-circ-buffer.h"

struct uart_ctx {
    struct circular_buffer circ_buf_tx;
    struct circular_buffer circ_buf_rx;

    struct uart_rx rx_data;
    struct uart_status status;
    bool enabled;
    bool tx_in_progress;
    bool tx_completed;
    bool want_to_tx;
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
    void (*uart_hw_init)(void);
    void (*uart_hw_deinit)(void);
    enum regmap_region ctrl_region;
    enum regmap_region status_region;
    enum regmap_region start_tx_region;
    enum regmap_region exchange_region;
    struct uart_ctx *ctx;
};

