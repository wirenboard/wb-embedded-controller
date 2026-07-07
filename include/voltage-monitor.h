#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

/* VMON channels names generation*/
#define __VMON_ENUM(vmon_name, adc_name, ok_min, ok_max, fail_min, fail_max)    VMON_CHANNEL_##vmon_name,

enum vmon_channel {
    VOLTAGE_MONITOR_DESC(__VMON_ENUM)
    VMON_CHANNEL_COUNT
};

void vmon_init(void);
bool vmon_ready(void);
bool vmon_get_ch_status(enum vmon_channel ch);
bool vmon_check_ch_once(enum vmon_channel ch);
void vmon_do_periodic_work(void);

// Перевзводит окно «устаканивания» после пробуждения из STM32 Stop: делает
// vmon снова «не готов» и перезапускает 100 мс задержку. АЦП в Stop стоит, и
// первый DMA-отсчёт после пробуждения переинициализирует фильтр одним сырым
// измерением; без перевзвода одиночный низкий V50 мог бы увести
// последовательность питания в Standby. Фильтр АЦП при этом НЕ трогаем.
void vmon_suspend_rearm_settle(void);
