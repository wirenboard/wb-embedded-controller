#pragma once
#include <stdint.h>
#include <stddef.h>

struct circular_buffer {
    uint8_t data[512];
    uint16_t head;
    uint16_t tail;
};

static inline uint16_t circ_buffer_get_used_space(const struct circular_buffer *buf)
{
    return (uint16_t)(buf->head - buf->tail);
}

static inline size_t cicr_buffer_get_available_space(const struct circular_buffer *buf)
{
    return sizeof(buf->data) - circ_buffer_get_used_space(buf);
}

static inline void circ_buffer_put(struct circular_buffer *buf, uint8_t byte)
{
    uint16_t byte_pos = buf->head % sizeof(buf->data);
    buf->head++;
    buf->data[byte_pos] = byte;
}

static inline uint8_t circ_buffer_push(struct circular_buffer *buf)
{
    uint16_t byte_pos = buf->tail % sizeof(buf->data);
    uint16_t byte = buf->data[byte_pos];
    buf->tail++;
    return byte;
}

static inline void circ_buffer_reset(struct circular_buffer *buf)
{
    buf->head = 0;
    buf->tail = 0;
}
