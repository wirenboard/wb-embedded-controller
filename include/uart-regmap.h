#pragma once
#include <stdint.h>
#include "uart-regmap-internal.h"

void uart_regmap_process_exchange(const struct uart_descr *u, union uart_exchange *e);
void uart_regmap_process_irq(const struct uart_descr *u);
void uart_regmap_process_ctrl(const struct uart_descr *u, const struct uart_ctrl *ctrl);
void uart_regmap_collect_data_for_new_exchange(const struct uart_descr *u);
bool uart_regmap_is_irq_needed(const struct uart_descr *u);
