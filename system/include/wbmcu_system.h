#pragma once
#include <stdint.h>
#include <stddef.h>

#if defined (STM32G030)
    #include "stm32g030xx.h"
    #define WB_MCU_PLATFORM_STM32G0
#endif

#define VECTOR_CORTEX_NUM               16
#define VECTOR_TABLE_SIZE               (VECTOR_CORTEX_NUM + VECTOR_PERIPH_NUM)

extern void (*vector_table[VECTOR_TABLE_SIZE])(void);

#define NVIC_SetHandler(irqn, handler)     vector_table[VECTOR_CORTEX_NUM + irqn] = handler

void Default_Handler(void);
