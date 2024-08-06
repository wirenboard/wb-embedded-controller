#pragma once
#include <stdint.h>

/* ====== Аппаратные ревизии ====== */

// Список всех аппаратных ревизий (hwrev).
// hwrev определяется делителем из резисторов подключенных к PC13
// и нужен для того, чтобы отличать одну плату от другой.
// Код 0 у WB74 по историческим причинам, т.к. в регистр на старых версиях выдавалось значение АЦП
// которое было около нуля
// Поле Code попадает в regmap в HW_INFO.hwrev и доступно в линуксе в /sys/bus/spi/drivers/wbec/spi0.0/hwrev
#define WBEC_HWREV_DESC(macro) \
        /*    Revesion name     Code   Res up  Res down */ \
        macro(WB74,             0,     100,    0         ) \
        macro(WB85,             85,    100,    22        ) \

// Допустимое отклонение в процентах от расчетной точки
// Пример: 100к/22к = 4095 * 22 / 122 = 738 единиц АЦП
// Допустимое отклонение = 3%, т.е. 22 единицы АЦП
#define WBEC_HWREV_DIFF_PERCENT                 3
// Также закладываем допустимое отклонение в единицах АЦП (шум самого АЦП)
#define WBEC_HWREV_DIFF_ADC                     10


/* ====== Общие GPIO для всех моделей ====== */
// Менять с осторожностью! Инициализируются независимо от HWREV

// Светодиод для индикации режима работы EC
// Установлен на плате, снаружи не виден
#define EC_GPIO_LED                             GPIOC, 6
#define EC_GPIO_LED_ACTIVE_HIGH

/* ====== Общие параметры для всех моделей ====== */

#define WBEC_DEBUG_MSG_PREFIX                   "[EC] "

// ID, лежит в карте регистров как константа
#define WBEC_ID                                 0x3CD2

/* ====== Параметры работы EC ====== */

// Таймаут, который устанавливается после включения питания
// Должен быть больше, чем время загрузки Linux
#define WBEC_WATCHDOG_INITIAL_TIMEOUT_S         120
// Максимальный таймаут
#define WBEC_WATCHDOG_MAX_TIMEOUT_S             600

// Время, на которое выключается питание при перезагрузке
#define WBEC_POWER_RESET_TIME_MS                1000
// Время загрузки Linux и драйверов WBEC
// До этого времени питание выключается сразу при коротком нажатии
// После - отправляется запрос в Linux
#define WBEC_LINUX_BOOT_TIME_MS                 20000
// Время от короткого нажатия кнопки (запрос в линукс) до сброса флага нажатия
// Нужно для того, чтобы отличать причину выключения
#define WBEC_LINUX_POWER_OFF_DELAY_MS           90000
// Время задержки включения при работе от USB
#define WBEC_LINUX_POWER_ON_DELAY_FROM_USB      5000

#define WBEC_PERIODIC_WAKEUP_FIRST_TIMEOUT_S    5
#define WBEC_PERIODIC_WAKEUP_NEXT_TIMEOUT_S     2

// Число попыток перезапуска PMIC при пропадании 3.3В
// Если за указанное время 3.3В пропадёт больше, чем указанное число раз,
// то EC выключит питание и уйдёт в спящий режим
#define WBEC_POWER_LOSS_TIMEOUT_MIN             10
#define WBEC_POWER_LOSS_ATTEMPTS                2

// Пищалка
#define EC_BUZZER_BEEP_FREQ                     1000
#define EC_BUZZER_BEEP_POWERON_MS               100
#define EC_BUZZER_BEEP_SHORT_PRESS_MS           300
#define EC_BUZZER_BEEP_LONG_PRESS_MS            1000

/* ====== Специфичные для моделей параметры ====== */

#if defined(MODEL_WB74)
    #include "config_wb74.h"
#elif defined(MODEL_WB85)
    #include "config_wb85.h"
#else
    #error "Unknown model"
#endif

