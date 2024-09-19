#pragma once
#include <stdint.h>
#include <stddef.h>

#define UART_REGMAP_CIRC_BUFFER_SIZE        512

struct circ_buf_index {
    uint16_t head;
    uint16_t tail;
};

struct circ_buf_rx {
    struct circ_buf_index i;
    struct data {
        uint8_t byte;
        uint8_t err_flags;
    } data[UART_REGMAP_CIRC_BUFFER_SIZE];
};

struct circ_buf_tx {
    struct circ_buf_index i;
    uint8_t data[UART_REGMAP_CIRC_BUFFER_SIZE];
};

static inline void circ_buffer_reset(struct circ_buf_index *i)
{
    i->head = 0;
    i->tail = 0;
}

static inline uint16_t circ_buffer_get_used_space(const struct circ_buf_index *i)
{
    return (uint16_t)(i->head - i->tail);
}

static inline size_t circ_buffer_get_available_space(const struct circ_buf_index *i)
{
    return UART_REGMAP_CIRC_BUFFER_SIZE - circ_buffer_get_used_space(i);
}

static inline void circ_buffer_tx_push(struct circ_buf_tx *buf, uint8_t byte)
{
    uint16_t byte_pos = buf->i.head % UART_REGMAP_CIRC_BUFFER_SIZE;
    buf->i.head++;
    buf->data[byte_pos] = byte;
}

static inline uint16_t circ_buffer_tx_pop(struct circ_buf_tx *buf)
{
    uint16_t byte_pos = buf->i.tail % UART_REGMAP_CIRC_BUFFER_SIZE;
    uint16_t byte = buf->data[byte_pos];
    buf->i.tail++;
    return byte;
}

static inline void circ_buffer_rx_push(struct circ_buf_rx *buf, uint8_t byte, uint8_t err_flags)
{
    uint16_t byte_pos = buf->i.head % UART_REGMAP_CIRC_BUFFER_SIZE;
    buf->i.head++;
    buf->data[byte_pos].byte = byte;
    buf->data[byte_pos].err_flags = err_flags;
}

static inline void circ_buffer_rx_pop(struct circ_buf_rx *buf, uint8_t *data, uint8_t *err_flags)
{
    uint16_t byte_pos = buf->i.tail % UART_REGMAP_CIRC_BUFFER_SIZE;
    *data = buf->data[byte_pos].byte;
    *err_flags = buf->data[byte_pos].err_flags;
    buf->i.tail++;
}
