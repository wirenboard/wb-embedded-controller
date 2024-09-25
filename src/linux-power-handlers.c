#include "config.h"
#include "uart-regmap-subsystem.h"
#include "usart_tx.h"
#include "gpio-subsystem.h"

// Дёргается, когда включается питание процессора (начинает грузиться линукс)
void linux_poweron_handler(void)
{
    usart_tx_deinit();
    gpio_reset();

    #if defined EC_UART_REGMAP_SUPPORT
        uart_regmap_subsystem_init();
    #endif
}
