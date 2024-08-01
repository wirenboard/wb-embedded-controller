#include "axp221s.h"
#include "config.h"
#include "software_i2c.h"

#if defined WBEC_WBMZ6_SUPPORT

#define AXP221S_ADDR            0x68

void axp221s_init(void)
{
    software_i2c_init();
}

bool axp221s_is_present(void)
{
    if (software_i2c_detect_device(I2C_PORT_WBMZ6, AXP221S_ADDR) == I2C_STATUS_OK) {
        return true;
    }
    return false;
}

#endif
