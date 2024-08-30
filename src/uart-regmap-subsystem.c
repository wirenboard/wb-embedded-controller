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

static bool wait_exchange[MOD_COUNT];
static bool exchange_received[MOD_COUNT];
static bool need_to_collect_data[MOD_COUNT];
static bool new_exchange_ready[MOD_COUNT];

static struct uart_ctx uart_ctx[MOD_COUNT] = {};

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
        .ctx = &uart_ctx[MOD1],
        .uart = USART1,
        .irq_num = USART1_IRQn,
        .uart_hw_init = mod1_uart_hw_init,
        .uart_hw_deinit = mod1_uart_hw_deinit,
        .ctrl_region = REGMAP_REGION_UART_CTRL_MOD1,
        .status_region = REGMAP_REGION_UART_STATUS_MOD1,
        .start_tx_region = REGMAP_REGION_UART_TX_START_MOD1,
        .exchange_region = REGMAP_REGION_UART_EXCHANGE_MOD1
    },
    [MOD2] = {
        .ctx = &uart_ctx[MOD2],
        .uart = USART2,
        .irq_num = USART2_IRQn,
        .uart_hw_init = mod2_uart_hw_init,
        .uart_hw_deinit = mod2_uart_hw_deinit,
        .ctrl_region = REGMAP_REGION_UART_CTRL_MOD2,
        .status_region = REGMAP_REGION_UART_STATUS_MOD2,
        .start_tx_region = REGMAP_REGION_UART_TX_START_MOD2,
        .exchange_region = REGMAP_REGION_UART_EXCHANGE_MOD2
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

    for (int i = 0; i < MOD_COUNT; i++) {
        need_to_collect_data[i] = true;
    }
}

void uart_regmap_subsystem_do_periodic_work(void)
{
    // Обработка региона управления и статуса
    for (int i = 0; i < MOD_COUNT; i++) {
        struct uart_ctrl uart_ctrl;
        if (regmap_get_data_if_region_changed(uart_descr[i].ctrl_region, &uart_ctrl, sizeof(uart_ctrl))) {
            uart_regmap_process_ctrl(&uart_descr[i], &uart_ctrl);
        }

        struct uart_status uart_status = {};
        uart_regmap_update_status(&uart_descr[i], &uart_status);
        regmap_set_region_data(uart_descr[i].status_region, &uart_status, sizeof(uart_status));
    }

    // Обработка региона начала передачи
    for (int i = 0; i < MOD_COUNT; i++) {
        struct uart_tx uart_tx_start;
        if (regmap_get_data_if_region_changed(uart_descr[i].start_tx_region, &uart_tx_start, sizeof(uart_tx_start))) {
            uart_regmap_process_start_tx(&uart_descr[i], &uart_tx_start);
            // need_to_collect_data[i] = true;
        }
    }

    // Обработка региона обмена
    if (irq_handled > 0) {
        for (int i = 0; i < MOD_COUNT; i++) {
            if (!exchange_received[i]) {
                union uart_exchange e;
                if (regmap_get_data_if_region_changed(uart_descr[i].exchange_region, &e, sizeof(e))) {
                    uart_regmap_process_exchange(&uart_descr[i], &e);
                    exchange_received[i] = true;
                    // wait_exchange[i] = false;
                }
            }
        }

        bool exchange_received_all = true;
        // bool has_enabled_ports = false;
        for (int i = 0; i < MOD_COUNT; i++) {
            // if (uart_ctx[i].enabled) {
            //     has_enabled_ports = true;
            if (!exchange_received[i]) {
                exchange_received_all = false;
            }
            // }
        }

        if (exchange_received_all) {
            for (int i = 0; i < MOD_COUNT; i++) {
                exchange_received[i] = false;
                need_to_collect_data[i] = true;
            }
            // Прерывание нужно сбросить после того, как обработаны данные для всех портов
            set_irq_gpio_inactive();
        }
    }

    // Обработка прерывания
    if (irq_handled < 0) {
        irq_handled++;
    } else if (irq_handled == 0) {
        bool irq_needed = false;

        for (int i = 0; i < MOD_COUNT; i++) {
            // if (uart_ctx[i].enabled) {
                if (need_to_collect_data[i]) {
                    uart_regmap_collect_data_for_new_exchange(&uart_descr[i]);
                }
                if (uart_regmap_is_irq_needed(&uart_descr[i])) {
                    // wait_exchange[i] = true;
                    irq_needed = true;
                }
            // }
        }

        if (irq_needed) {
            for (int i = 0; i < MOD_COUNT; i++) {
                if (need_to_collect_data[i]) {
                    if (regmap_set_region_data(uart_descr[i].exchange_region, &uart_descr[i].ctx->rx_data, sizeof(struct uart_rx))) {
                        need_to_collect_data[i] = false;
                        new_exchange_ready[i] = true;
                    }
                }
            }

            bool new_exchange_ready_all = true;
            // bool has_enabled_ports = false;
            for (int i = 0; i < MOD_COUNT; i++) {
                // if (uart_ctx[i].enabled) {
                    // has_enabled_ports = true;
                    if (!new_exchange_ready[i]) {
                        new_exchange_ready_all = false;
                    }
                // }
            }

            if (new_exchange_ready_all) {
                for (int i = 0; i < MOD_COUNT; i++) {
                    new_exchange_ready[i] = false;
                    need_to_collect_data[i] = false;
                    exchange_received[i] = false;
                }
                set_irq_gpio_active();
            }
        }
    }
}

