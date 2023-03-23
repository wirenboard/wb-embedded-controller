#pragma once
#include <stdint.h>
#include "wbmcu_system.h"

static inline void __primask_restore(uint32_t * saved_primask)
{
    __set_PRIMASK(*saved_primask);
}

static inline int __do_disable_irq(void) {
    __disable_irq();
    return 1;
}

/* Defines an atomic block

Expressions inside this atomic block won't be preempted by higher priority interrupt handlers.

On Cortex M0 and M23 this works by disabling all interrupts.
The interrupt state (PRIMASK) is restored after execution of this block, 
so ATOMIC blocks can be nested.

Usage:

ATOMIC {
    x();
    ATOMIC {
        z();
    }
    y();
}
*/
#define ATOMIC for (uint32_t primask_save __attribute__((__cleanup__(__primask_restore))) = __get_PRIMASK(), __todo = __do_disable_irq(); __todo; __todo = 0)

/*
cleanup (cleanup_function)
The cleanup attribute runs a function when the variable goes out of scope. This attribute can only be applied to auto function scope variables;
it may not be applied to parameters or variables with static storage duration. The function must take one parameter, a pointer to a type
compatible with the variable. The return value of the function (if any) is ignored.

https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html
*/
