#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "systick.h"

void linux_cpu_pwr_seq_init(bool on);
void linux_cpu_pwr_seq_off_and_goto_standby(uint16_t wakeup_after_s);
void linux_cpu_pwr_seq_on(void);
void linux_cpu_pwr_seq_wakeup(void);
// suspend-to-off: перед ПЕРВЫМ импульсом PWRON пробуждения выждать, пока с
// момента armed_ts (вооружение окна сна ≈ момент, когда BL31 усыпил PMIC)
// пройдёт не меньше WBEC_SUSPEND_WAKE_PWRON_MIN_DELAY_MS. Иначе PMIC, ещё не
// завершивший вход в сон, глотает импульс, и SoC не стартует. Запрашивается
// только при «мгновенном» выходе из окна (дедлайн-тиков WUT не было): при
// реальном сне переход PMIC давно завершён и пауза не нужна. Вызывать ПОСЛЕ
// linux_cpu_pwr_seq_wakeup() (тот сбрасывает запрос в «без паузы»).
void linux_cpu_pwr_seq_wake_pwron_min_delay(systime_t armed_ts);
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
