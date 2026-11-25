#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "regmap-int.h"

// Функции для тестирования regmap
void utest_regmap_reset(void);
void utest_regmap_mark_region_changed(enum regmap_region r);
bool utest_regmap_get_region_data(enum regmap_region r, void * data, size_t size);

// Занять regmap, как это делает идущая SPI-транзакция: regmap_set_region_data и
// regmap_get_data_if_region_changed возвращают false. Сбрасывается utest_regmap_reset()
void utest_regmap_set_busy(bool busy);

// Проверка вызова regmap_init
bool utest_regmap_was_init_called(void);
