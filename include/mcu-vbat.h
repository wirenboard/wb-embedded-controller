#pragma once

void mcu_vbat_init(void);
void mcu_vbat_check_do_periodic_work(void);

// Триггерит внеочередное измерение Vbat
void mcu_vbat_trigger_measurement(void);

// Перезапускает алгоритм зарядки: включает PWR_CR4_VBE и сбрасывает
// таймер зарядки на полный цикл.
void mcu_vbat_restart_charging(void);
