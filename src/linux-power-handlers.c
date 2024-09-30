#include "config.h"
#include "usart_tx.h"
#include "gpio-subsystem.h"

// Дёргается, когда включается питание процессора (начинает грузиться линукс)
void linux_poweron_handler(void)
{
    usart_tx_deinit();
    gpio_reset();
}
