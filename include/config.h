#pragma once
#include <stdint.h>

/* ====== Параметры работы EC ====== */

#define WBEC_DEBUG_MSG_PREFIX                   "[EC]: "

// Таймаут, который устанавливается после включения питания
// Должен быть больше, чем время загрузки Linux
#define WBEC_WATCHDOG_INITIAL_TIMEOUT_S         120
// Максимальный таймаут
#define WBEC_WATCHDOG_MAX_TIMEOUT_S             600

// ID, лежит в карте регистров как константа
#define WBEC_ID                                 0x3CD2

// Время, на которое выключается питание при перезагрузке
#define WBEC_POWER_RESET_TIME_MS                1000
// Время от короткого нажатия кнопки (запрос в линукс)
// до принудительного выключения (если линукс не ответил)
#define WBEC_LINUX_POWER_OFF_DELAY_MS           60000

#define WBEC_PERIODIC_WAKEUP_FIRST_TIMEOUT_S    5
#define WBEC_PERIODIC_WAKEUP_NEXT_TIMEOUT_S     2

// Температура, ниже которой EC не будет включаться
#define WBEC_MINIMUM_WORKING_TEMPERATURE_C_X100 -4000


/* ====== Подключения EC к Wiren Board ====== */

// Линия прерывания от EC в линукс
// Меняет состояние на активное, если в EC есть флаги событий
#define EC_GPIO_INT                     GPIOA, 8
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

// Модуль WBMZ
// WBMZ тянет вход вниз, если работает step-up на WBMZ
#define EC_GPIO_WBMZ_STATUS_BAT         GPIOB, 5
// В модуле WBMZ сигнал называется OFF и когда нужно отключить WBMZ, его нужно прижать вниз
// В WB 7.4.1 он подтянут вниз резистором 2к, т.е. по дефолту выключен
// Чтобы включить WBMZ, нужно выдать 1 на этот пин
#define EC_GPIO_WBMZ_ON                 GPIOB, 15

// Управляет питанием Linux
#define EC_GPIO_LINUX_POWER             GPIOD, 1
#define EC_GPIO_LINUX_PMIC_PWRON        GPIOB, 14
#define EC_GPIO_LINUX_PMIC_RESET_PWROK  GPIOB, 13


// USART TX - передача сообщений в Debug Console
// Выводит сообщения в ту же консоль, что и Linux
// Подключен через диод к DEBUG_TX
#define EC_DEBUG_USART_USE_USART1
#define EC_DEBUG_USART_BAUDRATE         115200
#define EC_DEBUG_USART_GPIO             GPIOA, 9
#define EC_DEBUG_USART_GPIO_AF          1



/* ====== Параметры RTC ====== */
// 0 - low, 1 - medium low, 2 - medium high, 3 - high
// Расчеты тут: https://docs.google.com/spreadsheets/d/1k51XYnHdV_j1-fccqVz4aFkKqSe-bGuWVDhoNqGtZ8E/edit#gid=1096674256
#define RTC_LSE_DRIVE_CAPABILITY        2

// Конфигурация АЦП
#define ADC_VREF_EXT_MV                 3300
#define NTC_RES_KOHM                    10
#define NTC_PULLUP_RES_KOHM             33

#define ADC_CHANNELS_DESC(macro) \
        /*    Channel name          ADC CH  PORT    PIN     RC      K               Offset, mV  */ \
        macro(ADC_IN1,              10,     GPIOB,  2,      50,     1,              0           ) \
        macro(ADC_IN2,              11,     GPIOB,  10,     50,     1,              0           ) \
        macro(ADC_IN3,              15,     GPIOB,  11,     50,     1,              0           ) \
        macro(ADC_IN4,              16,     GPIOB,  12,     50,     1,              0           ) \
        macro(ADC_V_IN,             9,      GPIOB,  1,      10,     212.0 / 12.0,   300         ) \
        macro(ADC_5V,               8,      GPIOB,  0,      10,     22.0 / 10.0,    0           ) \
        macro(ADC_3V3,              7,      GPIOA,  7,      10,     32.0 / 22.0,    0           ) \
        macro(ADC_NTC,              6,      GPIOA,  6,      50,     1,              0           ) \
        macro(ADC_VBUS_DEBUG,       3,      GPIOA,  3,      10,     2.2 / 1.0,      0           ) \
        macro(ADC_VBUS_NETWORK,     5,      GPIOA,  5,      10,     2.2 / 1.0,      0           ) \
        macro(ADC_HW_VER,           2,      GPIOA,  2,      50,     1,              0           ) \
        macro(ADC_INT_VREF,         13,     0,      0,      50,     1,              0           ) \


// Ожидание после старта прошивки и перед опросом напряжений
// Должно быть около 10RC канала ADC_5V
#define VOLTAGE_MONITOR_START_DELAY_MS      100

#define VOLTAGE_MONITOR_DESC(m) \
    /*Monitor CH        ADC CH              OK min  max         FAIL min max */ \
    m(V_OUT,            ADC_V_IN,           0,      28000,      0,      29000 ) \
    m(V33,              ADC_3V3,            3200,   3400,       3100,   3500  ) \
    m(V50,              ADC_5V,             4000,   5500,       3600,   5800  ) \
    m(VBUS_DEBUG,       ADC_VBUS_DEBUG,     4600,   5500,       4300,   5800  ) \
    m(VBUS_NETWORK,     ADC_VBUS_NETWORK,   4600,   5500,       4300,   5800  ) \

