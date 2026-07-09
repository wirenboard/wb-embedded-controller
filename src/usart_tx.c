#include "wbmcu_system.h"
#include "gpio.h"
#include "config.h"
#include "rcc.h"
#include "usart_tx.h"
#include <stdbool.h>

/**
 * Модуль позволяет передавать строки в отладочный UART.
 * Используется блокирующая передача.
 * Возможно передавать как null-terminated строки, так и явно указывать размер
 */

#ifdef EC_DEBUG_USART_USE_USART1
    static USART_TypeDef * const D_USART = USART1;
#else
    #error "Not supported USART"
#endif

#if defined EC_DEBUG_USART_GPIO
    static const gpio_pin_t usart_tx_gpio = { EC_DEBUG_USART_GPIO };
#else
    #include "shared-gpio.h"
#endif

static bool usart_initialized = false;

static inline void usart_transmit_char(char c)
{
    while ((D_USART->ISR & USART_ISR_TXE_TXFNF) == 0) {};
    D_USART->TDR = c;
}

static inline void usart_wait_tranmission_complete(void)
{
    // Ждём завершения передачи: TC = 1, когда последний кадр ПОЛНОСТЬЮ вышел
    // из сдвигового регистра. Условие было инвертировано: во время передачи
    // TC = 0, функция возвращалась мгновенно, и «блокирующая» печать
    // завершалась с 1-2 кадрами ещё в полёте. Это стреляло в одном месте:
    // печать маркера в такте вооружения окна suspend-to-off - через считанные
    // микросекунды EC уходил в Stop, USART замерзал ПОСРЕДИ КАДРА, и линия TX
    // висела на уровне бита данных всё окно. Отладочный мост питался мусором
    // (хвосты пробелов за маркером в каждом окне), а на длинном окне зависал
    // целиком - захват слепой (стенд 2026-07-09 00:37, «кнопка молчит»).
    // Цена честного ожидания: <= 2 кадра ~ 170 мкс на печать.
    while ((D_USART->ISR & USART_ISR_TC) == 0) {};
}

static inline void init_debug_uart_if_not_initialized(void)
{
    if (!usart_initialized) {
        usart_tx_init();
    }
}

void usart_tx_init(void)
{
    #if defined EC_DEBUG_USART_GPIO
        GPIO_S_SET_OUTPUT(usart_tx_gpio);
        GPIO_S_SET_AF(usart_tx_gpio, EC_DEBUG_USART_GPIO_AF);
    #else
        // debug uart делит gpio PA9 с MOD1_TX
        shared_gpio_set_mode(MOD1, MOD_GPIO_TX, MOD_GPIO_MODE_PA9_AF_DEBUG_UART);
    #endif


    #ifdef EC_DEBUG_USART_USE_USART1
        NVIC_DisableIRQ(USART1_IRQn);
        NVIC_ClearPendingIRQ(USART1_IRQn);

        RCC->APBENR2 |= RCC_APBENR2_USART1EN;

        // Reset USART
        RCC->APBRSTR2 |= RCC_APBRSTR2_USART1RST;
        RCC->APBRSTR2 &= ~RCC_APBRSTR2_USART1RST;
    #endif

    D_USART->BRR = SystemCoreClock / EC_DEBUG_USART_BAUDRATE;
    D_USART->CR1 |= USART_CR1_TE | USART_CR1_UE;

    usart_initialized = true;
}

void usart_tx_deinit(void)
{
    #if defined EC_DEBUG_USART_GPIO
        GPIO_S_SET_INPUT(usart_tx_gpio);
    #else
        if (shared_gpio_get_mode(MOD1, MOD_GPIO_TX) != MOD_GPIO_MODE_PA9_AF_DEBUG_UART) {
            return;
        }
        shared_gpio_set_mode(MOD1, MOD_GPIO_TX, MOD_GPIO_MODE_INPUT);
    #endif

    #ifdef EC_DEBUG_USART_USE_USART1
        // Reset USART
        RCC->APBRSTR2 |= RCC_APBRSTR2_USART1RST;
        RCC->APBRSTR2 &= ~RCC_APBRSTR2_USART1RST;

        RCC->APBENR2 &= ~RCC_APBENR2_USART1EN;
    #endif

    usart_initialized = false;
}

void usart_tx_buf_blocking(const void * buf, size_t size)
{
    init_debug_uart_if_not_initialized();

    for (size_t i = 0; i < size; i++) {
        usart_transmit_char(((const char *)buf)[i]);
    }
    usart_wait_tranmission_complete();
}

void usart_tx_str_blocking(const char str[])
{
    init_debug_uart_if_not_initialized();

    while (*str) {
        usart_transmit_char(*str);
        str++;
    }
    usart_wait_tranmission_complete();
}
