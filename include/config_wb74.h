#pragma once

// Температура, ниже которой EC не будет включать линукс
// Проверка происходит однократно при включении, в процессе работы уже не проверяется
// При температуре ниже указанной ЕС будет ждать и выводить сообщения, пока температура не поднимется
#define WBEC_MINIMUM_WORKING_TEMPERATURE        -40.0

/* ====== Подключения EC к Wiren Board ====== */

// Линия прерывания от EC в линукс
// Меняет состояние на активное, если в EC есть флаги событий
#define EC_GPIO_INT                             GPIOA, 8
#define EC_GPIO_INT_ACTIVE_HIGH

// Светодиод для индикации режима работы EC
// Установлен на плате, снаружи не виден
#define EC_GPIO_LED                             GPIOC, 6
#define EC_GPIO_LED_ACTIVE_HIGH

// Кнопка включения
#define EC_GPIO_PWRKEY                          GPIOA, 0
#define EC_GPIO_PWRKEY_ACTIVE_LOW
#define EC_GPIO_PWRKEY_WKUP_NUM                 1
// Для игнорирования случайных нажатий во время открывания/закрывания крышки
// или случайного прикосновения к корпусу дебаунс нужно делать большим
// значение 500 мс подобрано экспериментально
#define PWRKEY_DEBOUNCE_MS                      500
#define PWRKEY_LONG_PRESS_TIME_MS               8000

// Включение V_OUT
#define EC_GPIO_VOUT_EN                         GPIOA, 15

// Модуль WBMZ
// WBMZ тянет вход вниз, если работает step-up на WBMZ
#define EC_GPIO_WBMZ_STATUS_BAT                 GPIOB, 5
// В модуле WBMZ сигнал называется OFF и когда нужно отключить WBMZ, его нужно прижать вниз
// В WB 7.4.1 он подтянут вниз резистором 2к, т.е. по дефолту выключен
// Чтобы включить WBMZ, нужно выдать 1 на этот пин
#define EC_GPIO_WBMZ_ON                         GPIOB, 15

// Управляет питанием Linux
#define EC_GPIO_LINUX_POWER                     GPIOD, 1
#define EC_GPIO_LINUX_PMIC_PWRON                GPIOB, 14
#define EC_GPIO_LINUX_PMIC_RESET_PWROK          GPIOB, 13


// USART TX - передача сообщений в Debug Console
// Выводит сообщения в ту же консоль, что и Linux
// Подключен через диод к DEBUG_TX
#define EC_DEBUG_USART_USE_USART1
#define EC_DEBUG_USART_BAUDRATE                 115200
#define EC_DEBUG_USART_GPIO                     GPIOA, 9
#define EC_DEBUG_USART_GPIO_AF                  1



/* ====== Параметры RTC ====== */
// 0 - low, 1 - medium low, 2 - medium high, 3 - high
// Расчеты тут: https://docs.google.com/spreadsheets/d/1k51XYnHdV_j1-fccqVz4aFkKqSe-bGuWVDhoNqGtZ8E/edit#gid=1096674256
#define RTC_LSE_DRIVE_CAPABILITY                2

// Конфигурация АЦП
#define ADC_VREF_EXT_MV                         3300
#define NTC_RES_KOHM                            10
#define NTC_PULLUP_RES_KOHM                     33

#define ADC_CHANNELS_DESC(macro) \
        /*    Channel name          ADC CH  PORT    PIN     RC      K               Offset, mV  */ \
        macro(ADC_IN1,              10,     GPIOB,  2,      50,     1,              0           ) \
        macro(ADC_IN2,              11,     GPIOB,  10,     50,     1,              0           ) \
        macro(ADC_IN3,              15,     GPIOB,  11,     50,     1,              0           ) \
        macro(ADC_IN4,              16,     GPIOB,  12,     50,     1,              0           ) \
        macro(ADC_V_IN,             9,      GPIOB,  1,      10,     212.0 / 12.0,   400         ) \
        macro(ADC_5V,               8,      GPIOB,  0,      10,     22.0 / 10.0,    0           ) \
        macro(ADC_3V3,              7,      GPIOA,  7,      10,     32.0 / 22.0,    0           ) \
        macro(ADC_NTC,              6,      GPIOA,  6,      50,     1,              0           ) \
        macro(ADC_VBUS_DEBUG,       3,      GPIOA,  3,      10,     2.2 / 1.0,      0           ) \
        macro(ADC_VBUS_NETWORK,     5,      GPIOA,  5,      10,     2.2 / 1.0,      0           ) \
        macro(ADC_HW_VER,           17,     GPIOA,  13,     50,     1,              0           ) \
        macro(ADC_INT_VREF,         13,     0,      0,      50,     1,              0           ) \

// macro(ADC_HW_VER,           17,      GPIOA,  13,     50,     1,              0           )

// Ожидание после старта прошивки и перед опросом напряжений
// Должно быть около 10RC канала ADC_5V
#define VOLTAGE_MONITOR_START_DELAY_MS      100

/**
 * Особенности моониторинга напряжений:
 *  - предел измерения V_IN - 58.3 В
 *  - V_OUT нужно отключать при низком V_IN (при питании от USB)
 *  - V_OUT нужно отключать при V_IN более 28 В
 *  - после старта PMIC напряжение на линии 3.3В фактически 3.0В, после ~150-200 мс меняется на 3.3В
 *    время зависит видимо от объема памяти на разных контроллерах может отличаться, доходя до >1000 мс
 *    поэтому для канала V33 выбираем диапазон, который покрывает 3.0 - 3.3 В
 *    любое напряжение из указанного диапазона будет означать, что PMIC работает
 */
#define VOLTAGE_MONITOR_DESC(m) \
    /*Monitor CH        ADC CH              OK min  max         FAIL min max */ \
    m(V_IN,             ADC_V_IN,           6500,   58000,      6000,   59000 ) \
    m(V_OUT,            ADC_V_IN,           6500,   28000,      6000,   29000 ) \
    m(V_IN_FOR_WBMZ,    ADC_V_IN,           11500,  58000,      11000,  59000 ) \
    m(V33,              ADC_3V3,            2900,   3400,       2800,   3500  ) \
    m(V50,              ADC_5V,             4000,   5500,       3600,   5800  ) \
    m(VBUS_DEBUG,       ADC_VBUS_DEBUG,     4000,   5500,       3600,   5800  ) \
    m(VBUS_NETWORK,     ADC_VBUS_NETWORK,   4000,   5500,       3600,   5800  ) \

