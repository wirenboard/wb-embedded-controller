#pragma once

#define MODEL_WB74
#include "include/config.h"

#ifdef ACTIVE_LOW
    #ifdef EC_GPIO_PWRKEY_ACTIVE_HIGH
        #undef EC_GPIO_PWRKEY_ACTIVE_HIGH
        #define EC_GPIO_PWRKEY_ACTIVE_LOW
    #endif
#elif defined ACTIVE_HIGH
    #ifdef EC_GPIO_PWRKEY_ACTIVE_LOW
        #undef EC_GPIO_PWRKEY_ACTIVE_LOW
        #define EC_GPIO_PWRKEY_ACTIVE_HIGH
    #endif
#else
    #error "Unknown test configuration"
#endif
