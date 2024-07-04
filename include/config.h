#pragma once
#include <stdint.h>

/* ====== Аппаратные ревизии ====== */

#define WBEC_HWREV_DESC(macro) \
        /*    Revesion name          Code   Res up  Res down */ \
        macro(WBEC_HWREV_WB74,       74,    100,    0         ) \
        macro(WBEC_HWREV_WB85,       85,    100,    22        ) \

// Допустимое отклонение в процентах от расчетной точки
// Пример: 100к/22к = 4095 * 22 / 122 = 738 единиц АЦП
// Допустимое отклонение = 3%, т.е. 22 единицы АЦП
#define WBEC_HWREV_DIFF_PERCENT                 3
// Также закладываем допустимое отклонение в единицах АЦП (шум самого АЦП)
#define WBEC_HWREV_DIFF_ADC                     10


/* ====== Общие параметры для всех моделей ====== */

#define WBEC_DEBUG_MSG_PREFIX                   "[EC] "

// ID, лежит в карте регистров как константа
#define WBEC_ID                                 0x3CD2


/* ====== Специфичные для моделей параметры ====== */

#if defined(MODEL_WB74)
    #include "config_wb74.h"
#elif defined(MODEL_WB85)
    #include "config_wb85.h"
#else
    #error "Unknown model"
#endif

