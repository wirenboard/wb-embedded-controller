#pragma once
#include <stddef.h>

void usart_init(void);

// Transmits buffer with given size
void usart_tx_buf_blocking(const void * buf, size_t size);
// Transmits null-terminated string
void usart_tx_str_blocking(const char str[]);
