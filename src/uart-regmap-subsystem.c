#include "uart-regmap-internal.h"
#include "shared-gpio.h"
#include "regmap-structs.h"
#include "gpio.h"
#include "config.h"
#include "uart-regmap.h"
#include <string.h>

#define UART_REGMAP_PORTS_COUNT         2

static const gpio_pin_t usart_irq_gpio = { EC_GPIO_UART_INT };

static int irq_handled = false;

static void mod1_uart_hw_init(void)
{
    RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;

    SYSCFG->CFGR1 |= SYSCFG_CFGR1_PA11_RMP;

    RCC->APBENR2 |= RCC_APBENR2_USART1EN;
    RCC->APBRSTR2 |= RCC_APBRSTR2_USART1RST;
    RCC->APBRSTR2 &= ~RCC_APBRSTR2_USART1RST;

    shared_gpio_set_mode(MOD1, MOD_GPIO_TX, MOD_GPIO_MODE_AF_UART);
    shared_gpio_set_mode(MOD1, MOD_GPIO_RX, MOD_GPIO_MODE_AF_UART);
    shared_gpio_set_mode(MOD1, MOD_GPIO_DE, MOD_GPIO_MODE_AF_UART);
}

static void mod1_uart_hw_deinit(void)
{
    SYSCFG->CFGR1 &= ~SYSCFG_CFGR1_PA11_RMP;

    RCC->APBRSTR2 |= RCC_APBRSTR2_USART1RST;
    RCC->APBRSTR2 &= ~RCC_APBRSTR2_USART1RST;
    RCC->APBENR2 &= ~RCC_APBENR2_USART1EN;

    shared_gpio_set_mode(MOD1, MOD_GPIO_TX, MOD_GPIO_MODE_DEFAULT);
    shared_gpio_set_mode(MOD1, MOD_GPIO_RX, MOD_GPIO_MODE_DEFAULT);
    shared_gpio_set_mode(MOD1, MOD_GPIO_DE, MOD_GPIO_MODE_DEFAULT);
}

static void mod2_uart_hw_init(void)
{
    RCC->APBENR1 |= RCC_APBENR1_USART2EN;
    RCC->APBRSTR1 |= RCC_APBRSTR1_USART2RST;
    RCC->APBRSTR1 &= ~RCC_APBRSTR1_USART2RST;

    shared_gpio_set_mode(MOD2, MOD_GPIO_TX, MOD_GPIO_MODE_AF_UART);
    shared_gpio_set_mode(MOD2, MOD_GPIO_RX, MOD_GPIO_MODE_AF_UART);
    shared_gpio_set_mode(MOD2, MOD_GPIO_DE, MOD_GPIO_MODE_AF_UART);
}

static void mod2_uart_hw_deinit(void)
{
    RCC->APBRSTR1 |= RCC_APBRSTR1_USART2RST;
    RCC->APBRSTR1 &= ~RCC_APBRSTR1_USART2RST;
    RCC->APBENR1 &= ~RCC_APBENR1_USART2EN;

    shared_gpio_set_mode(MOD2, MOD_GPIO_TX, MOD_GPIO_MODE_DEFAULT);
    shared_gpio_set_mode(MOD2, MOD_GPIO_RX, MOD_GPIO_MODE_DEFAULT);
    shared_gpio_set_mode(MOD2, MOD_GPIO_DE, MOD_GPIO_MODE_DEFAULT);
}

static const struct uart_descr uart_descr[MOD_COUNT] = {
    [MOD1] = {
        .uart = USART1,
        .irq_num = USART1_IRQn,
        .uart_hw_init = mod1_uart_hw_init,
        .uart_hw_deinit = mod1_uart_hw_deinit,
        .ctrl_region = REGMAP_REGION_UART_CTRL_MOD1,
        .status_region = REGMAP_REGION_UART_STATUS_MOD1
    },
    [MOD2] = {
        .uart = USART2,
        .irq_num = USART2_IRQn,
        .uart_hw_init = mod2_uart_hw_init,
        .uart_hw_deinit = mod2_uart_hw_deinit,
        .ctrl_region = REGMAP_REGION_UART_CTRL_MOD2,
        .status_region = REGMAP_REGION_UART_STATUS_MOD2
    },
};

static void mod1_uart_irq_handler(void)
{
    uart_regmap_process_irq(&uart_descr[MOD1]);
}

static void mod2_uart_irq_handler(void)
{
    uart_regmap_process_irq(&uart_descr[MOD2]);
}

static inline void set_irq_gpio_active(void)
{
    irq_handled = 1;
    GPIO_S_SET(usart_irq_gpio);
}

static inline void set_irq_gpio_inactive(void)
{
    irq_handled = -1;
    GPIO_S_RESET(usart_irq_gpio);
}


void uart_regmap_subsystem_init(void)
{
    GPIO_S_SET_PUSHPULL(usart_irq_gpio);
    GPIO_S_SET_OUTPUT(usart_irq_gpio);

    NVIC_SetHandler(USART1_IRQn, mod1_uart_irq_handler);
    NVIC_SetHandler(USART2_IRQn, mod2_uart_irq_handler);
}

void uart_regmap_subsystem_do_periodic_work(void)
{
    for (int i = 0; i < MOD_COUNT; i++) {
        struct uart_ctrl uart_ctrl;
        if (regmap_get_data_if_region_changed(uart_descr[i].ctrl_region, &uart_ctrl, sizeof(uart_ctrl))) {
            uart_regmap_process_ctrl(&uart_descr[i], &uart_ctrl);
        }

        struct uart_status uart_status;
        uart_regmap_update_status(&uart_descr[i], &uart_status);
        regmap_set_region_data(uart_descr[i].status_region, &uart_status, sizeof(uart_status));
    }

    struct REGMAP_UART_TX_START uart_tx_start;
    if (regmap_get_data_if_region_changed(REGMAP_REGION_UART_TX_START, &uart_tx_start, sizeof(uart_tx_start))) {
        if (uart_tx_start.port_num < MOD_COUNT) {
            uint8_t port_num = uart_tx_start.port_num;
            uart_regmap_process_start_tx(&uart_descr[port_num], &uart_tx_start.tx);
        }
    }

    // union uart_exchange uart_exchange;
    struct REGMAP_UART_EXCHANGE uart_exchange_regmap;
    if (regmap_get_data_if_region_changed(REGMAP_REGION_UART_EXCHANGE, &uart_exchange_regmap, sizeof(uart_exchange_regmap))) {
        // получаем и отправляем данные для обоих портов сразу

        for (int i = 0; i < MOD_COUNT; i++) {
            uart_regmap_process_exchange(&uart_descr[i], &uart_exchange_regmap.e[i]);
        }

        set_irq_gpio_inactive();
    }

    if (irq_handled < 0) {
        irq_handled++;
    } else if (irq_handled == 0) {
        bool irq_needed = false;

        for (int i = 0; i < MOD_COUNT; i++) {
            uart_regmap_collect_data_for_new_exchange(&uart_descr[i]);
            if (uart_regmap_is_irq_needed(&uart_descr[i])) {
                irq_needed = true;
            }
        }

        if (irq_needed) {
            for (int i = 0; i < MOD_COUNT; i++) {
                memcpy(&uart_exchange_regmap.e[i], &uart_descr[i].ctx->rx_data, sizeof(struct uart_rx));
                if (regmap_set_region_data(REGMAP_REGION_UART_EXCHANGE, &uart_exchange_regmap, sizeof(uart_exchange_regmap))) {
                    set_irq_gpio_active();
                }
            }
        }
    }
}

