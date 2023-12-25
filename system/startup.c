#include "wbmcu_system.h"

extern int main(void);
void Default_Reset_Handler(void);

extern uint32_t _sidata;        // start of .data section initial values in FLASH
extern uint32_t _sdata;         // start of .data section in RAM
extern uint32_t _edata;         // end of .data section in RAM
extern uint32_t _sbss;          // start of .bss section in RAM
extern uint32_t _ebss;          // end of .bss section in RAM
extern uint32_t __stack;        // stack location

// Entry record in FLASH
__attribute__ ((used, section(".entry_record")))
void (* const EntryRecord[])(void) = {
    (void * const)&__stack,
    Default_Reset_Handler
};

// NVIC vectors table in ram array
__attribute__ ((used, section(".isr_vector")))
void (*vector_table[VECTOR_TABLE_SIZE])(void);

void Default_Reset_Handler(void)
{
// init vector table
    for (uint16_t i = 0; i < VECTOR_TABLE_SIZE; i++) {
        vector_table[i] = &Default_Handler;
    }

// copy data section from flash
    uint32_t * pulSrc = &_sidata;
    uint32_t * pulDest = &_sdata;
    while (pulDest < &_edata) {
        *(pulDest++) = *(pulSrc++);
    }

// clean bss section
    pulDest = &_sbss;
    while (pulDest < &_ebss) {
        *(pulDest++) = 0;
    }

// move vector table to ram
#if (__CORTEX_M == 0)
    SYSCFG->CFGR1 |= SYSCFG_CFGR1_MEM_MODE;     // Embedded SRAM mapped at 0x0000 0000
#else
    SCB->VTOR |= (uint32_t)vector_table;        // NVIC Vector Table Offset Register - VTOR (0xE000ED08)
#endif

// run main application
    main();
}

void Default_Handler(void)
{
    while (1) {};
}
