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

struct uart_ctrl {
    uint16_t port_num;
    uint16_t enable : 1;
    uint16_t baud_x100;
    uint16_t parity : 2;
    uint16_t stop_bits : 2;
};

struct uart_status {
    uint16_t enabled : 1;
    uint16_t reserved : 15;
};

union uart_exchange {
    struct uart_rx rx;
    struct uart_tx tx;
};
