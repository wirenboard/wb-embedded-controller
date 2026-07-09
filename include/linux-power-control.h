#pragma once
#include <stdbool.h>
#include <stdint.h>

void linux_cpu_pwr_seq_init(bool on);
void linux_cpu_pwr_seq_off_and_goto_standby(uint16_t wakeup_after_s);
void linux_cpu_pwr_seq_on(void);
void linux_cpu_pwr_seq_wakeup(void);
// suspend-to-off: запросить бип-подтверждение кнопочного пробуждения. Пищалка
// (SG1) запитана от 3.3В, которое во время окна сна ВЫКЛЮЧЕНО - звук физически
// невозможен до рестарта PMIC. Бип выдаётся последовательностью пробуждения в
// первый момент, когда 3.3В вернулось, а SoC ещё удержан в сбросе (безопасно
// для DRAM: ре-инициализация ещё не началась); импульс PWROK откладывается до
// конца бипа.
void linux_cpu_pwr_seq_wake_beep_request(void);
void linux_cpu_pwr_seq_hard_off(void);
void linux_cpu_pwr_seq_hard_reset(void);
void linux_cpu_pwr_seq_reset_pmic(void);
void linux_cpu_pwr_seq_warm_reset(void);
bool linux_cpu_pwr_seq_is_busy(void);
void linux_cpu_pwr_seq_do_periodic_work(void);
