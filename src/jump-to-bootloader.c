#include "regmap-int.h"
#include "wbmcu_system.h"
#include "config.h"

#define FLASH_OPTKEY1       ((uint32_t)0x08192A3B)  /*!< Flash option byte key1 */
#define FLASH_OPTKEY2       ((uint32_t)0x4C5D6E7F)  /*!< Flash option byte key2: used with FLASH_OPTKEY1
                                                                    to unlock the write access to the option byte block */

#define FLASH_FKEY1         ((uint32_t)0x45670123)  /*!< Flash program erase key1 */
#define FLASH_FKEY2         ((uint32_t)0xCDEF89AB)  /*!< Flash program erase key2: used with FLASH_PEKEY1
                                                                    to unlock the write access to the FPEC. */

static inline void flash_unlock(void)
{
    FLASH->KEYR = FLASH_FKEY1;
    FLASH->KEYR = FLASH_FKEY2;
}

static inline void option_bytes_unlock(void)
{
    // not check FLASH->CR & FLASH_CR_OPTWRE before wrike keys for save flash space
    // Unlock option byte
    FLASH->OPTKEYR = FLASH_OPTKEY1;
    FLASH->OPTKEYR = FLASH_OPTKEY2;
}

static void jump_to_bootloader(void)
{
    __disable_irq();

    flash_unlock();
    option_bytes_unlock();

    // nBOOT_SEL = 1 : BOOT0 signal is defined by nBOOT0 option bit
    // nBOOT0 = 0; nBOOT1 = 1 : boot from system memory
    // no RDP protection
    const uint32_t target_value = 0xDBFFE1AA;

    FLASH->OPTR = target_value;
    while (FLASH->SR & FLASH_SR_BSY1) {};
    FLASH->CR |= FLASH_CR_OPTSTRT;
    while (FLASH->SR & FLASH_SR_BSY1) {};

    // Reload option byte (will trigger system reset)
    FLASH->CR |= FLASH_CR_OBL_LAUNCH;
}

void jump_to_bootloader_do_periodic_work(void)
{
    struct REGMAP_JUMP_TO_BOOT reg;
    if (regmap_get_data_if_region_changed(REGMAP_REGION_JUMP_TO_BOOT, &reg, sizeof(reg))) {
        if (reg.jump_cmd == WBEC_ID) {
            jump_to_bootloader();
        }
    }
}
