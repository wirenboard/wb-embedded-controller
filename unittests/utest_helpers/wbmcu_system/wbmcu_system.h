#pragma once
#include <stdint.h>

// Mock заголовок для wbmcu_system.h

// Mock для UID_BASE (определяется в stm32g030xx.h)
extern const uint32_t uid_base_mock[3];
#define UID_BASE ((uint32_t *)uid_base_mock)

// Mock для NVIC_SystemReset
void NVIC_SystemReset(void);

typedef struct {
	volatile uint32_t CR1;
	volatile uint32_t CR2;
	volatile uint32_t CR3;
	volatile uint32_t CR4;
	volatile uint32_t SR1;
	volatile uint32_t SR2;
	volatile uint32_t SCR;
	volatile uint32_t PUCRA;
	volatile uint32_t PDCRA;
	volatile uint32_t PUCRB;
	volatile uint32_t PDCRB;
	volatile uint32_t PUCRC;
	volatile uint32_t PDCRC;
	volatile uint32_t PUCRD;
	volatile uint32_t PDCRD;
} PWR_TypeDef;

extern PWR_TypeDef pwr_mock;

#define PWR ((PWR_TypeDef *)&pwr_mock)
#define PWR_CR3_APC (1U << 10)
