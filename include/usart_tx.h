#pragma once

void usart_init(void);
void usart_tx_str_blocking(const char str[], size_t size);
void usart_tx_strn_blocking(const char str[]);
