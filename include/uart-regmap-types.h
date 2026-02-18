#pragma once
#include <stdint.h>

#define UART_REGMAP_BUFFER_SIZE             64

enum uart_word_length {
    // Имеется в виду количество бит данных без учёта бита чётности, даже если он включён.
    // Такая логика выбрана для обратной совместимости (ранее было всегда 8 бит), чтобы
    // при активации бита чётности не изменялась фактическая длина данных на передачу/приём,
    // а просто добавлялся бит контроля чётности.
    UART_WORD_LEN_8 = 0,
    UART_WORD_LEN_7 = 1,

    UART_WORD_LEN_MAX_VALUE = UART_WORD_LEN_7
};

enum uart_parity {
    UART_PARITY_NONE = 0,
    UART_PARITY_EVEN = 1,
    UART_PARITY_ODD = 2,

    UART_PARITY_MAX_VALUE = UART_PARITY_ODD
};

enum uart_stop_bits {
    // values according to STM32G0 reference manual
    UART_STOP_BITS_1 = 0,
    UART_STOP_BITS_0_5 = 1,
    UART_STOP_BITS_2 = 2,
    UART_STOP_BITS_1_5 = 3,
};

union uart_rx_byte_w_errors {
    struct {
        uint8_t err_flags;
        uint8_t byte;
    };
    uint16_t byte_w_errors;
};

struct uart_start_tx {
    uint16_t want_to_tx;
};

struct uart_rx {
    // количество прочитанных байт в текущей транзакции
    uint8_t read_bytes_count;
    // флаг означает, что ЕС готов к передаче: в следующей транзакции можно отправить данные
    uint8_t ready_for_tx : 1;
    // флаг означает, что передача завершена
    uint8_t tx_completed : 1;
    // формат данных: 0 - байты, 1 - байты с ошибками
    uint8_t data_format : 1;
    uint8_t reserved : 5;
    union {
        union uart_rx_byte_w_errors bytes_with_errors[UART_REGMAP_BUFFER_SIZE / 2];
        uint8_t read_bytes[UART_REGMAP_BUFFER_SIZE];
    };
};

struct uart_tx {
    uint8_t bytes_to_send_count;
    // регистры 16 бит, зарезервируем 1 байт, чтобы данные начинались с нового регистра
    uint8_t reserved;
    uint8_t bytes_to_send[UART_REGMAP_BUFFER_SIZE];
};

struct uart_ctrl {
    /* offset 0x00 */
    uint16_t enable : 1;
    uint16_t ctrl_applyed : 1;
    /* offset 0x01 */
    uint16_t baud_x100;
    /* offset 0x02 */
    uint16_t parity : 2;
    uint16_t stop_bits : 2;
    uint16_t rs485_enabled : 1;
    uint16_t rs485_rx_during_tx : 1;
    uint16_t word_length : 2; // reserve 2 bits for 0/1 value for optional adding 6-,9-data bits modes in future
};

union uart_exchange {
    struct uart_rx rx;
    struct uart_tx tx;
};
