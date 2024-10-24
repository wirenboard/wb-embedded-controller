#pragma once
#include <stddef.h>

void usart_tx_init(void);
void usart_tx_deinit(void);

// Transmits buffer with given size
void usart_tx_buf_blocking(const void * buf, size_t size);
// Transmits null-terminated string
void usart_tx_str_blocking(const char str[]);
