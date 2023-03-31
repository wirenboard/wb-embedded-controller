#include "wbmcu_system.h"
#include "gpio.h"
#include "config.h"

/**
 * Модуль позволяет передавать строки в отладочный UART.
 * Используется блокирующая передача.
 * Возможно передавать как null-terminated строки, так и явновно указывать размер
 */

static const gpio_pin_t usart_tx_gpio = { EC_DEBUG_USART_GPIO };

static inline void usart_transmit_char(char c)
{
    while ((USART1->ISR & USART_ISR_TXE_TXFNF) == 0) {};
    USART1->TDR = c;
}

static inline void usart_wait_tranmission_complete(void)
{
    while (USART1->ISR & USART_ISR_TC) {};
}


void usart_init(void)
{
    // Init GPIO
    GPIO_S_SET_OUTPUT(usart_tx_gpio);
    GPIO_S_SET_AF(usart_tx_gpio, EC_DEBUG_USART_GPIO_AF);

    #ifdef EC_USE_USART1_DEBUG_TX
        RCC->APBENR2 |= RCC_APBENR2_USART1EN;
        USART1->BRR = F_CPU / EC_DEBUG_USART_BAUDRATE;
        USART1->CR1 |= USART_CR1_TE | USART_CR1_UE;
    #else
        #error "Not supported USART"
    #endif
}

void usart_tx_str_blocking(const char str[], size_t size)
{
    for (size_t i = 0; i < size; i++) {
        usart_transmit_char(str[i]);
    }
    usart_wait_tranmission_complete();
}

void usart_tx_strn_blocking(const char str[])
{
    while (*str) {
        usart_transmit_char(*str);
        str++;
    }
    usart_wait_tranmission_complete();
}
