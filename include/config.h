#pragma once

#define F_CPU       16000000

#define PWR_KEY_PORT        GPIOA
#define PWR_KEY_PIN         0


// TODO Replace with 2500
#define ADC_VREF_EXT_MV                 3300

// TODO Add other channels
#define ADC_CHANNELS_DESC(macro)        macro(MCU_TEMP, 12, ADC_NO_GPIO_PIN, ADC_NO_GPIO_PIN, 32), \
                                        macro(MCU_VDDA, 13, ADC_NO_GPIO_PIN, ADC_NO_GPIO_PIN, 50)
