#pragma once
#include <stdint.h>

#define WBEC_SPI_SLAVE_INACTIVITY_TIMEOUT_MS        1000


// Инициализирует SPI Slave и EXTI на линии CS
void spi_slave_init(void);

// Возвращает время в миллисекундах с момента последней активности по SPI
uint32_t spi_slave_get_time_since_last_transaction(void);
