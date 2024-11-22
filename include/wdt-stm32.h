#include "wbmcu_system.h"

#define WDG_KR_KEY_RELOAD                   0xAAAA
#define WDG_KR_KEY_ENABLE                   0xCCCC
#define WDG_KR_KEY_ACCESS_ENABLE            0x5555

#define WDG_LSI_FREQ                        32000

// период в 10 секунд выбран, т.к. ЕС уходит в спячку и не может обработать watchdog
// спит он максимум 5 секунд
#define WDG_PERIOD_MS                       10000

static inline void watchdog_init(void)
{
    // according to RM0454, section 21.3.2

    IWDG->KR = WDG_KR_KEY_ENABLE;

    IWDG->KR = WDG_KR_KEY_ACCESS_ENABLE;
    IWDG->PR = 0b110 << IWDG_PR_PR_Pos; // prescaler = 256 gives 125 Hz
    IWDG->RLR = WDG_PERIOD_MS * WDG_LSI_FREQ / 256 / 1000;

    while (IWDG->SR) {};

    IWDG->KR = WDG_KR_KEY_RELOAD;
}

static inline void watchdog_reload(void)
{
    IWDG->KR = WDG_KR_KEY_RELOAD;
}
