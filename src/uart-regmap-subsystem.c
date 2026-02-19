#include "config.h"

#if defined EC_UART_REGMAP_SUPPORT

#include "uart-regmap-internal.h"
#include "shared-gpio.h"
#include "regmap-structs.h"
#include "gpio.h"
#include "bits.h"
#include "uart-regmap.h"
#include <string.h>

#define UART_REGMAP_PORTS_COUNT         2

static const gpio_pin_t usart_irq_gpio = { EC_GPIO_UART_INT };

// Битовые флаги для обработки процесса обмена данными по spi
// Необходимость этих флагов обусловлена тем, что регион exchange записывается линуксом за 1 раз для всех портов,
// а собрать данные нужно по отдельности для каждого порта
struct spi_exchange_flags {
    // произведён обмен (exchange) данными
    uint8_t exchange_received;
    // необходимо собрать данные для нового обмена
    uint8_t need_to_collect_data;
    // новый обмен готов к передаче
    uint8_t new_exchange_ready;
};

static bool irq_handled = false;
static bool uart_subsystem_initialized = false;

static struct spi_exchange_flags spi_exchange_flags;
static struct uart_ctx uart_ctx[MOD_COUNT] = {};

static const struct uart_descr uart_descr[MOD_COUNT] = {
    [MOD1] = {
        .ctx = &uart_ctx[MOD1],
        .uart = USART1,
        .irq_num = USART1_IRQn,
        .ctrl_region = REGMAP_REGION_UART_CTRL_MOD1,
        .start_tx_region = REGMAP_REGION_UART_TX_START_MOD1,
        .exchange_region = REGMAP_REGION_UART_EXCHANGE_MOD1
    },
    [MOD2] = {
        .ctx = &uart_ctx[MOD2],
        .uart = USART2,
        .irq_num = USART2_IRQn,
        .ctrl_region = REGMAP_REGION_UART_CTRL_MOD2,
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
    // Нужно обеспечивать минимальный интервал сбросом и повторной установкой линии прерывания,
    // иначе линукс может не увидеть его
    // Необходимый интервао около 200 мкс обеспечивается длительностью основого цикла
    irq_handled = true;
    GPIO_S_SET(usart_irq_gpio);
}

static inline void set_irq_gpio_inactive(void)
{
    irq_handled = false;
    GPIO_S_RESET(usart_irq_gpio);
}

void uart_regmap_subsystem_init(void)
{
    GPIO_S_SET_PUSHPULL(usart_irq_gpio);
    GPIO_S_SET_OUTPUT(usart_irq_gpio);

    RCC->APBENR2 |= RCC_APBENR2_USART1EN;
    RCC->APBRSTR2 |= RCC_APBRSTR2_USART1RST;
    RCC->APBRSTR2 &= ~RCC_APBRSTR2_USART1RST;

    RCC->APBENR1 |= RCC_APBENR1_USART2EN;
    RCC->APBRSTR1 |= RCC_APBRSTR1_USART2RST;
    RCC->APBRSTR1 &= ~RCC_APBRSTR1_USART2RST;

    NVIC_SetHandler(USART1_IRQn, mod1_uart_irq_handler);
    NVIC_SetHandler(USART2_IRQn, mod2_uart_irq_handler);

    spi_exchange_flags.exchange_received = 0;
    spi_exchange_flags.need_to_collect_data = BIT_MASK(MOD_COUNT);
    spi_exchange_flags.new_exchange_ready = 0;

    for (int i = 0; i < MOD_COUNT; i++) {
        NVIC_DisableIRQ(uart_descr[i].irq_num);
        NVIC_ClearPendingIRQ(uart_descr[i].irq_num);

        memset(&uart_ctx[i], 0, sizeof(struct uart_ctx));

        uart_ctx[i].ctrl.baud_x100 = 1152;
        uart_ctx[i].ctrl.parity = UART_PARITY_NONE;
        uart_ctx[i].ctrl.stop_bits = UART_STOP_BITS_1;
        uart_ctx[i].ctrl.word_length = UART_WORD_LEN_8;

        uart_ctx[i].rx_data.ready_for_tx = 1;

        regmap_set_region_data(uart_descr[i].ctrl_region, &uart_ctx[i].ctrl, sizeof(uart_ctx[i].ctrl));
        regmap_set_region_data(uart_descr[i].exchange_region, &uart_descr[i].ctx->rx_data, sizeof(struct uart_rx));
    }
    set_irq_gpio_inactive();
    uart_subsystem_initialized = true;
}

void uart_regmap_subsystem_do_periodic_work(void)
{
    if (!uart_subsystem_initialized) {
        return;
    }

    // Обработка региона управления и статуса
    for (int i = 0; i < MOD_COUNT; i++) {
        struct uart_ctrl uart_ctrl_from_regmap;
        if (regmap_get_data_if_region_changed(uart_descr[i].ctrl_region, &uart_ctrl_from_regmap, sizeof(uart_ctrl_from_regmap))) {
            uart_regmap_process_ctrl(&uart_descr[i], &uart_ctrl_from_regmap);
        }

        if (uart_ctx[i].ctrl.ctrl_applyed) {
            if (regmap_set_region_data(uart_descr[i].ctrl_region, &uart_ctx[i].ctrl, sizeof(uart_ctx[i].ctrl))) {
                // ctrl_applyed сбрасывается после того, как данные успешно записаны в regmap
                // и одновременно используется как флаг успешной записи в regmap
                // в итоге в regmap ctrl_applyed = 1
                uart_ctx[i].ctrl.ctrl_applyed = 0;
            }
        }
    }

    // Обработка региона начала передачи
    for (int i = 0; i < MOD_COUNT; i++) {
        struct uart_start_tx uart_tx_start;
        // регион START_TX по сути write-only, запись 1 поверх 1 также ставит флаг region_changed
        // нет необходимости сбрасывать флаг в регмапе
        if (regmap_get_data_if_region_changed(uart_descr[i].start_tx_region, &uart_tx_start, sizeof(uart_tx_start))) {
            if (uart_tx_start.want_to_tx) {
                // будет сброшен при установки линии прерывания в активное состояние
                uart_ctx[i].want_to_tx = true;
            }
        }
    }

    // Обработка региона обмена
    if (irq_handled) {
        for (int i = 0; i < MOD_COUNT; i++) {
            if ((spi_exchange_flags.exchange_received & BIT(i)) == 0) {
                // если для порта ещё не получена структура exchange, то проверяем, есть ли она
                union uart_exchange e;
                if (regmap_get_data_if_region_changed(uart_descr[i].exchange_region, &e, sizeof(e))) {
                    uart_regmap_process_exchange(&uart_descr[i], &e);
                    // и устанавливаем флаг, что данные получены
                    spi_exchange_flags.exchange_received |= BIT(i);
                }
            }
        }

        if (spi_exchange_flags.exchange_received == BIT_MASK(MOD_COUNT)) {
            // получены данные для всех портов - нужно сбросить прерывание
            spi_exchange_flags.exchange_received = 0;
            // и выставить флаг, что пора собирать данные для нового обмена
            spi_exchange_flags.need_to_collect_data = BIT_MASK(MOD_COUNT);
            set_irq_gpio_inactive();
        }
    } else {
        // Обработка прерывания
        bool irq_needed = false;

        for (int i = 0; i < MOD_COUNT; i++) {
            if (spi_exchange_flags.need_to_collect_data & BIT(i)) {
                // нужно вызывать до тех пор, пока regmap_set_region_data не вернет true
                // с каждым новым вызовом данные будут пополняться, если это возможно
                uart_regmap_collect_data_for_new_exchange(&uart_descr[i]);
            }
            if (uart_regmap_is_irq_needed(&uart_descr[i])) {
                irq_needed = true;
            }
        }

        if (irq_needed) {
            // если один из портов хочет прерывание - забираем из всех портов данные для exchange и помещаем в regmap
            for (int i = 0; i < MOD_COUNT; i++) {
                if (spi_exchange_flags.need_to_collect_data & BIT(i)) {
                    if (regmap_set_region_data(uart_descr[i].exchange_region, &uart_descr[i].ctx->rx_data, sizeof(struct uart_rx))) {
                        // сбрасываем флаг - данные в regmap, собирать их больше не нужно
                        spi_exchange_flags.need_to_collect_data &= ~BIT(i);
                        // и выставляем флаг, что порт готов к новому обмену
                        spi_exchange_flags.new_exchange_ready |= BIT(i);
                    }
                }
            }

            if (spi_exchange_flags.new_exchange_ready == BIT_MASK(MOD_COUNT)) {
                // все порты готовы к новому обмену - устанавливаем прерывание и сбрасываем флаги
                spi_exchange_flags.new_exchange_ready = 0;
                spi_exchange_flags.need_to_collect_data = 0;
                spi_exchange_flags.exchange_received = 0;
                for (int i = 0; i < MOD_COUNT; i++) {
                    uart_ctx[i].want_to_tx = false;
                }
                set_irq_gpio_active();
            }
        }
    }
}

#endif
