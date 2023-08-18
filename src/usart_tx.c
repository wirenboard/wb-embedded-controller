#include "wbmcu_system.h"
#include "gpio.h"
#include "config.h"
#include "rcc.h"

/**
 * Модуль позволяет передавать строки в отладочный UART.
 * Используется блокирующая передача.
 * Возможно передавать как null-terminated строки, так и явновно указывать размер
 */

#ifdef EC_DEBUG_USART_USE_USART1
    static USART_TypeDef * const D_USART = USART1;
#else
    #error "Not supported USART"
#endif

static const gpio_pin_t usart_tx_gpio = { EC_DEBUG_USART_GPIO };


static inline void usart_transmit_char(char c)
{
    while ((D_USART->ISR & USART_ISR_TXE_TXFNF) == 0) {};
    D_USART->TDR = c;
}

static inline void usart_wait_tranmission_complete(void)
{
    while (D_USART->ISR & USART_ISR_TC) {};
}


void usart_init(void)
{
    // Init GPIO
    GPIO_S_SET_OUTPUT(usart_tx_gpio);
    GPIO_S_SET_AF(usart_tx_gpio, EC_DEBUG_USART_GPIO_AF);

    #ifdef EC_DEBUG_USART_USE_USART1
        RCC->APBENR2 |= RCC_APBENR2_USART1EN;

        // Reset USART
        RCC->APBRSTR2 |= RCC_APBRSTR2_USART1RST;
        RCC->APBRSTR2 &= ~RCC_APBRSTR2_USART1RST;
    #endif

    D_USART->BRR = SystemCoreClock / EC_DEBUG_USART_BAUDRATE;
    D_USART->CR1 |= USART_CR1_TE | USART_CR1_UE;
}

void usart_tx_buf_blocking(const void * buf, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        usart_transmit_char(((const char *)buf)[i]);
    }
    usart_wait_tranmission_complete();
}

void usart_tx_str_blocking(const char str[])
{
    while (*str) {
        usart_transmit_char(*str);
        str++;
    }
    usart_wait_tranmission_complete();
}
