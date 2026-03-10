#pragma once
#include <stdint.h>

// Mock заголовок для wbmcu_system.h

// Mock для UID_BASE (определяется в stm32g030xx.h)
extern const uint32_t uid_base_mock[3];
#define UID_BASE ((uint32_t *)uid_base_mock)

// Mock для NVIC_SystemReset
void NVIC_SystemReset(void);

// Mock для PWR (определяется в stm32g030xx.h)
typedef struct {
  volatile uint32_t CR1;        /*!< PWR Power Control Register 1,                     Address offset: 0x00 */
  volatile uint32_t RESERVED0;  /*!< Reserved,                                         Address offset: 0x04 */
  volatile uint32_t CR3;        /*!< PWR Power Control Register 3,                     Address offset: 0x08 */
  volatile uint32_t CR4;        /*!< PWR Power Control Register 4,                     Address offset: 0x0C */
  volatile uint32_t SR1;        /*!< PWR Power Status Register 1,                      Address offset: 0x10 */
  volatile uint32_t SR2;        /*!< PWR Power Status Register 2,                      Address offset: 0x14 */
  volatile uint32_t SCR;        /*!< PWR Power Status Clear Register,                  Address offset: 0x18 */
  uint32_t RESERVED1;           /*!< Reserved,                                         Address offset: 0x1C */
  volatile uint32_t PUCRA;      /*!< PWR Pull-Up Control Register of port A,           Address offset: 0x20 */
  volatile uint32_t PDCRA;      /*!< PWR Pull-Down Control Register of port A,         Address offset: 0x24 */
  volatile uint32_t PUCRB;      /*!< PWR Pull-Up Control Register of port B,           Address offset: 0x28 */
  volatile uint32_t PDCRB;      /*!< PWR Pull-Down Control Register of port B,         Address offset: 0x2C */
  volatile uint32_t PUCRC;      /*!< PWR Pull-Up Control Register of port C,           Address offset: 0x30 */
  volatile uint32_t PDCRC;      /*!< PWR Pull-Down Control Register of port C,         Address offset: 0x34 */
  volatile uint32_t PUCRD;      /*!< PWR Pull-Up Control Register of port D,           Address offset: 0x38 */
  volatile uint32_t PDCRD;      /*!< PWR Pull-Down Control Register of port D,         Address offset: 0x3C */
  uint32_t RESERVED2;           /*!< Reserved,                                         Address offset: 0x40 */
  uint32_t RESERVED3;           /*!< Reserved,                                         Address offset: 0x44 */
  volatile uint32_t PUCRF;      /*!< PWR Pull-Up Control Register of port F,           Address offset: 0x48 */
  volatile uint32_t PDCRF;      /*!< PWR Pull-Down Control Register of port F,         Address offset: 0x4C */
} PWR_TypeDef;

extern PWR_TypeDef _PWR_instance;

#define PWR (&_PWR_instance)
