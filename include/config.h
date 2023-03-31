#pragma once
#include <stdint.h>

#define F_CPU       64000000

// Таймаут, который устанавливается после включения питания
// Должен быть больше, чем время загрузки Linux
#define WDEC_WATCHDOG_INITIAL_TIMEOUT_S         60
// Максимальный таймаут
#define WDEC_WATCHDOG_MAX_TIMEOUT_S             600


// Линия прерывания от EC в линукс
// Меняет состояние на активное, если в EC есть флаги событий
#define EC_GPIO_INT                     GPIOB, 5
#define EC_GPIO_INT_ACTIVE_HIGH

// Светодиод для индикации режима работы EC
// Установлен на плате, снаружи не виден
#define EC_GPIO_LED                     GPIOC, 6
#define EC_GPIO_LED_ACTIVE_HIGH

// Кнопка включения
#define EC_GPIO_PWRKEY                  GPIOA, 0
#define EC_GPIO_PWRKEY_ACTIVE_LOW
#define EC_GPIO_PWRKEY_WKUP_NUM         1
#define PWRKEY_DEBOUNCE_MS              50
#define PWRKEY_LONG_PRESS_TIME_MS       3000

// Включение V_OUT
#define EC_GPIO_VOUT_EN                 GPIOA, 15

// Состояние модуля WBMZ
// WBMZ тянет вход вниз, если работает step-up на WBMZ
#define EC_GPIO_STATUS_BAT              GPIOB, 5


// Конфигурация АЦП
#define ADC_VREF_EXT_MV                 2500
#define ADC_VREF_EXT_EN_GPIO            GPIOD, 3
#define NTC_RES_KOHM                    10
#define NTC_PULLUP_RES_KOHM             33

#define ADC_CHANNELS_DESC(macro) \
        /*    Channel name          ADC CH  PORT    PIN     RC      K              */ \
        macro(ADC_IN1,              10,     GPIOB,  2,      50,     1               ) \
        macro(ADC_IN2,              11,     GPIOB,  10,     50,     1               ) \
        macro(ADC_IN3,              15,     GPIOB,  11,     50,     1               ) \
        macro(ADC_IN4,              16,     GPIOB,  12,     50,     1               ) \
        macro(ADC_V_IN,             9,      GPIOB,  1,      50,     210.0 / 10.0    ) \
        macro(ADC_5V,               8,      GPIOB,  0,      50,     22.0 / 10.0     ) \
        macro(ADC_3V3,              7,      GPIOA,  7,      50,     32.0 / 22.0     ) \
        macro(ADC_NTC,              6,      GPIOA,  6,      50,     1               ) \
        macro(ADC_VBUS_DEBUG,       4,      GPIOA,  4,      50,     22.0 / 10.0     ) \
        macro(ADC_VBUS_NETWORK,     5,      GPIOA,  5,      50,     22.0 / 10.0     ) \
        macro(ADC_HW_VER,           2,      GPIOA,  2,      50,     1               ) \

