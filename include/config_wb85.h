#pragma once
#include <stdint.h>

/* ====== Аппаратная ревизия ====== */

#define WBEC_HWREV                              HWREV_WB85

// Температура, ниже которой EC не будет включать линукс
// Проверка происходит однократно при включении, в процессе работы уже не проверяется
// При температуре ниже указанной ЕС будет ждать и выводить сообщения, пока температура не поднимется
#define WBEC_MINIMUM_WORKING_TEMPERATURE        -20.0

// Количество незначащих слов между записью адреса и началом передачи данных.
// Нужно, чтобы подготовить данные без паузы между передачей адреса и началом передачи данных.
// Пауза запускает планирование в линуксе, которое может растянуться на неопределенное время
#define SPI_SLAVE_PAD_WORDS_COUNT                5      // 5 слов по 16 бит @ 1 МГц = 80 мкс

/* ====== Подключения EC к Wiren Board ====== */

// Линия прерывания от EC в линукс
// Используется только для работы UART-ов
#define EC_GPIO_UART_INT                        GPIOA, 8

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
#define EC_GPIO_VOUT_EN                         GPIOD, 0

// Модуль WBMZ
// WBMZ тянет вход вниз, если работает step-up на WBMZ
#define EC_GPIO_WBMZ_STATUS_BAT                 GPIOB, 5
// Включение повышающего преобразователя на WBMZ
#define EC_GPIO_WBMZ_STEPUP_ENABLE              GPIOB, 15
// Разрешение заряда WBMZ
// Зяряд нужно разрешать, когда работаем от Vin (от USB заряд д.б. запрещен)
#define WBEC_GPIO_WBMZ_CHARGE_ENABLE            GPIOB, 4

// WBMZ6-BATTERY имеет на борту свой собственный PMIC для контроля заряда и других параметров
#define WBEC_WBMZ6_SUPPORT
#define WBEC_WBMZ6_POLL_PERIOD_MS                       100
// Параметры WBMZ6-BATTERY
#define WBEC_WBMZ6_BATTERY_POLL_PERIOD_MS               100
#define WBEC_WBMZ6_BATTERY_CHARGE_CURRENT_MA            600
#define WBEC_WBMZ6_BATTERY_FULL_DESIGN_CAPACITY_MAH     2600
#define WBEC_WBMZ6_BATTERY_VOLTAGE_MIN_MV               2900
#define WBEC_WBMZ6_BATTERY_VOLTAGE_MAX_MV               4100
#define WBEC_WBMZ6_BATTERY_HIGH_TEMP_CHARGE_LIMIT       40.0
#define WBEC_WBMZ6_BATTERY_NTC_RES_KOHM                 10
// Температурные пороги задаются в мВ на пине TS
// При расчете следует учитывать сопротивление резистора последовательно с NTC
// Посчитать можно тут: https://docs.google.com/spreadsheets/d/1fvdiSBb0WEPnSeek40awh9ejFEPKrPeAPRvstHpcSgI/edit?gid=0#gid=0
// VLTF-charge - дефолтное значение 3.9 °C
// VHTF-charge - дефолтное значение 59.5 °C
// VLTF-discharge - дефолтное значение 5.6 °C
#define WBEC_WBMZ6_BATTERY_VHTF_DISCHARGE_VADC_MV       499     // 48.5 °C
// Параметры WBMZ6-SUPERCAP
#define WBEC_WBMZ6_SUPERCAP_DETECT_VOLTAGE_MV           500
#define WBEC_WBMZ6_SUPERCAP_VOLTAGE_MAX_MV              4950
#define WBEC_WBMZ6_SUPERCAP_VOLTAGE_MIN_MV              3000
#define WBEC_WBMZ6_SUPERCAP_CAPACITY_MF                 25000   // 25 Farad = 2 series 50F capacitors
#define WBEC_WBMZ6_SUPERCAP_CHARGE_CURRENT_MA           230
// Ток, по модулю меньший этого значения, будет зануляться
#define WBEC_WBMZ6_SUPERCAP_CURRENT_ZEROING_MA          80

// Управляет питанием Linux
#define EC_GPIO_LINUX_POWER                     GPIOD, 1
#define EC_GPIO_LINUX_PMIC_PWRON                GPIOD, 3
#define EC_GPIO_LINUX_PMIC_RESET_PWROK          GPIOB, 3


// USART TX - передача сообщений в Debug Console
// Выводит сообщения в ту же консоль, что и Linux
// Подключен через диод к DEBUG_TX
#define EC_DEBUG_USART_USE_USART1
#define EC_DEBUG_USART_BAUDRATE                 115200

// Пищалка
// Поддерживается только GPIOC, 7!
#define EC_GPIO_BUZZER                          GPIOC, 7

// Нагреватель
#define EC_GPIO_HEATER                          GPIOD, 2
#define EC_HEATER_ON_TEMP                       -15.0
#define EC_HEATER_OFF_TEMP                      -10.0

// Один USB разъем на DEBUG и NETWORK
#define EC_USB_HUB_DEBUG_NETWORK

// EC управляет gpio на MOD1 и MOD2
#define EC_MOD1_MOD2_GPIO_CONTROL

// Поддержка spi-uart
#define EC_UART_REGMAP_SUPPORT

//                                                    name     freq    sda        scl
#define SOFTWARE_I2C_DESC(macro)                macro(WBMZ6,   100000, GPIOF, 1,  GPIOF, 0)


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
        macro(ADC_3V3,              6,      GPIOA,  6,      10,     32.0 / 22.0,    0           ) \
        macro(ADC_NTC,              5,      GPIOA,  5,      50,     1,              0           ) \
        macro(ADC_VBUS_DEBUG,       3,      GPIOA,  3,      10,     2.2 / 1.0,      0           ) \
        macro(ADC_VBAT,             7,      GPIOA,  7,      10,     200.0 / 100.0,  0           ) \
        macro(ADC_HW_VER,           17,     GPIOA,  13,     50,     1,              0           ) \
        macro(ADC_INT_VREF,         13,     0,      0,      50,     1,              0           ) \

// Ожидание после старта прошивки и перед опросом напряжений
// Должно быть около 10RC канала ADC_5V
#define VOLTAGE_MONITOR_START_DELAY_MS          100

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

