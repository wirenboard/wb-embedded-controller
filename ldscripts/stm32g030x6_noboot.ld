/*
 * Wirenboard linker script for STM32G030X6 devices
 *
 * vector table dynamic created in RAM in startup code
 *
 */

MEMORY
{
    RAM (xrw)       : ORIGIN = 0x20000000,      LENGTH = 8K
    FLASH (rx)      : ORIGIN = 0x08000000,      LENGTH = 32K
}
