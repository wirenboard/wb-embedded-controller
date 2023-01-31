#pragma once

#define F_CPU       16000000

// TODO Add other channels
#define ADC_CHANNELS_DESC(macro)        macro(V3_3, 6, GPIOA, 6, 32), \
                                        macro(V5_0, 7, GPIOA, 7, 32), \
                                        macro(MCU_TEMP, 16, ADC_NO_GPIO_PIN, ADC_NO_GPIO_PIN, 32), \
                                        macro(MCU_VDDA, 17, ADC_NO_GPIO_PIN, ADC_NO_GPIO_PIN, 50)
