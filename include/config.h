#pragma once
#include <stdint.h>

#define F_CPU       16000000

#define PWRKEY_DEBOUNCE_MS                  50
#define PWRKEY_LONG_PRESS_TIME_MS           3000

#define PWR_KEY_PORT        GPIOA
#define PWR_KEY_PIN         0

#define INT_PORT            GPIOB
#define INT_PIN             9

#define SYSTEM_LED_PORT     GPIOB
#define SYSTEM_LED_PIN      1


#define WBEC_ID             0xD2

#ifndef MODBUS_DEVICE_FW_VERSION_NUMBERS
#define MODBUS_DEVICE_FW_VERSION_NUMBERS 1,0,0,0
#endif


// TODO Replace with 2500
#define ADC_VREF_EXT_MV                 3300

// TODO Add other channels
#define ADC_CHANNELS_DESC(macro)        macro(MCU_TEMP, 12, ADC_NO_GPIO_PIN, ADC_NO_GPIO_PIN, 32), \
                                        macro(MCU_VDDA, 13, ADC_NO_GPIO_PIN, ADC_NO_GPIO_PIN, 50)
