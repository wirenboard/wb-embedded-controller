#pragma once
#include <stdbool.h>
#include <stdint.h>

void linux_cpu_pwr_seq_init(bool on);
void linux_cpu_pwr_seq_off_and_goto_standby(uint16_t wakeup_after_s);
// suspend-to-off: уводит EC в STM32 Standby, ОСТАВЛЯЯ 5В включённым (модульный
// ключ 5В держит внешняя схема при high-Z пине - PMIC хранит DRAM в
// self-refresh). Пробуждение (heartbeat WUT / будильник / кнопка) - это сброс
// EC; его разбирает wbec_init(). Не возвращается.
void linux_cpu_pwr_seq_suspend_to_standby(uint16_t wakeup_after_s);
// Пробуждение из suspend-to-off Standby: берёт (держащийся внешней схемой) пин
// 5В под драйвер без провала уровня и взводит последовательность пробуждения
// PMIC (импульс PWROK после появления 3.3В). Вызывать один раз из wbec_init.
void linux_cpu_pwr_seq_resume_init(void);
void linux_cpu_pwr_seq_on(void);
void linux_cpu_pwr_seq_wakeup(void);
void linux_cpu_pwr_seq_hard_off(void);
void linux_cpu_pwr_seq_hard_reset(void);
void linux_cpu_pwr_seq_reset_pmic(void);
void linux_cpu_pwr_seq_warm_reset(void);
bool linux_cpu_pwr_seq_is_busy(void);
void linux_cpu_pwr_seq_do_periodic_work(void);
