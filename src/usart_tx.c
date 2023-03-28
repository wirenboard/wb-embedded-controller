#include "wbmcu_system.h"
#include "gpio.h"
#include "config.h"

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
    GPIO_SET_OUTPUT(GPIOA, 9);
    GPIO_SET_AF(GPIOA, 9, 1);

    // Init USART1
    RCC->APBENR2 |= RCC_APBENR2_USART1EN;
    USART1->BRR = F_CPU / 115200;
    USART1->CR1 |= USART_CR1_TE | USART_CR1_UE;
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
