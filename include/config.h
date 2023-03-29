#pragma once
#include <stdint.h>

#define F_CPU       64000000

#define PWRKEY_DEBOUNCE_MS                  50
#define PWRKEY_LONG_PRESS_TIME_MS           3000

#define EC_GPIO_LINUX_POWER                 GPIOD, 2

#define PWR_KEY_PORT        GPIOA
#define PWR_KEY_PIN         0

#define INT_PORT            GPIOB
#define INT_PIN             5

#define GPIO_VREF_EN_PORT   GPIOD
#define GPIO_VREF_EN_PIN    1

#define GPIO_VOUT_EN_PORT   GPIOA
#define GPIO_VOUT_EN_PIN    15

#define WBEC_ID             0xD2

#ifndef MODBUS_DEVICE_FW_VERSION_NUMBERS
#define MODBUS_DEVICE_FW_VERSION_NUMBERS 1,0,0,0
#endif

#define WDEC_WATCHDOG_INITIAL_TIMEOUT_S         60

#define WBEC_POWER_RESET_TIME_MS                500
#define WBEC_LINUX_POWER_OFF_DELAY_MS           60000


#define ADC_VREF_EXT_MV                 3300 /* TODO 2500 */
#define NTC_RES_KOHM                    10
#define NTC_PULLUP_RES_KOHM             36 /*TODO 33 */

// TODO Add other channels
#define ADC_CHANNELS_DESC(macro) \
        /*    Channel name          ADC CH  PORT    PIN     RC      K          */ \
        macro(ADC_IN1,              1,      GPIOA,  1,      50,     55.0 / 2.7  ) \
        macro(ADC_IN2,              2,      GPIOA,  2,      50,     55.0 / 2.7  ) \
        macro(ADC_IN3,              3,      GPIOA,  3,      50,     55.0 / 2.7  ) \
        macro(ADC_IN4,              4,      GPIOA,  4,      50,     55.0 / 2.7  ) \
        macro(ADC_V_IN,             5,      GPIOA,  5,      50,     50.9 / 3.9  /* TODO 55.0 / 2.7 */  ) \
        macro(ADC_5V,               6,      GPIOA,  6,      50,     23.3 / 10.0 /* TODO 22.0 / 10.0 */ ) \
        macro(ADC_3V3,              7,      GPIOA,  7,      50,     32.0 / 22.0 ) \
        macro(ADC_NTC,              8,      GPIOB,  0,      50,     1.0         ) \
        macro(ADC_VBUS_DEBUG,       11,     GPIOB,  10,      50,     22.0 / 10.0 ) /* TODO CH9, GPIOB1 */ \
        macro(ADC_VBUS_NETWORK,     10,     GPIOB,  2,      50,     22.0 / 10.0 ) \
        macro(ADC_HW_VER,           15,     GPIOB,  11,     50,     1.0         ) \

// Линия прерывания от EC в линукс
// Меняет состояние на активное, если в EC есть флаги событий
#define EC_GPIO_INT                     GPIOB, 5
#define EC_GPIO_INT_ACTIVE_HIGH

// Светодиод для индикации режима работы EC
// Установлен на плате, снаружи не виден
// TODO GPIOC, 6
#define EC_GPIO_LED                     GPIOB, 1
#define EC_GPIO_LED_ACTIVE_HIGH
