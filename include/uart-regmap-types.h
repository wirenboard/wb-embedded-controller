#pragma once
#include <stdint.h>

#define UART_REGMAP_BUFFER_SIZE             64

struct uart_rx {
    uint8_t read_bytes_count;
    uint8_t ready_for_tx;
    uint8_t read_bytes[UART_REGMAP_BUFFER_SIZE];
};

struct uart_tx {
    uint8_t bytes_to_send_count;
    uint8_t reserved;
    uint8_t bytes_to_send[UART_REGMAP_BUFFER_SIZE];
};

union uart_exchange {
    struct uart_rx rx;
    struct uart_tx tx;
};
