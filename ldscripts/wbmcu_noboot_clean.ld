/*
 * Wirenboard common linker script
 *
 *      vector table dynamic created in RAM in startup code
 *      clean build without bootloader, modbus and storage
 *
 */

__stack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .text :                         /* executed code in flash*/
    {
        . = ALIGN(4);
        KEEP(*(.entry_record))      /* stack pointer and Reset vector */
        . = ALIGN(4);
        *(.text)                    /* .text sections (code) */
        *(.text*)                   /* .text* sections (code) */
        . = ALIGN(4);
    } >FLASH

    .isr_vector (NOLOAD) :          /* Allocate place for all vectors in begin of RAM */
    {
        . = ALIGN(4);
        _vectors_ptr = .;
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >RAM

    .rodata :                       /* Constant data goes into FLASH */
    {
        . = ALIGN(4);
        *(.rodata)                  /* .rodata sections (constants, strings, etc.) */
        *(.rodata*)                 /* .rodata* sections (constants, strings, etc.) */
        . = ALIGN(4);
    } >FLASH

    _sidata = LOADADDR(.data);      /* used by the startup to initialize data */

    .data :                         /* Initialized data sections goes into RAM, load LMA copy after code */
    {
        . = ALIGN(4);
        _sdata = .;                 /* create a global symbol at data start */
        *(.data)                    /* .data sections */
        *(.data*)                   /* .data* sections */
        *(.ram_func)                /* .ram_func sections */
        . = ALIGN(4);
        _edata = .;                 /* define a global symbol at data end */
    } >RAM AT> FLASH

    . = ALIGN(4);                   /* Uninitialized data section */
    .bss :
    {
        _sbss = .;                  /* define a global symbol at bss start */
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;                  /* define a global symbol at bss end */
    } >RAM

    ._check_stack : ALIGN(4)        /* check space for stack */
    {
        . = . + _Minimum_Stack_Size;
    } >RAM
}
