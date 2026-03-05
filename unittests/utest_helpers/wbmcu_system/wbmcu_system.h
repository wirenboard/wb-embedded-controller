#pragma once
#include <stdint.h>

// Mock заголовок для wbmcu_system.h

// Mock для UID_BASE (определяется в stm32g030xx.h)
extern const uint32_t uid_base_mock[3];
#define UID_BASE ((uint32_t *)uid_base_mock)

// Mock для NVIC_SystemReset
void NVIC_SystemReset(void);
