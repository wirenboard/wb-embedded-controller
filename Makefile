#######################################
# MODEL defines list
#######################################

MODEL_LIST = \
MODEL_WB74


#######################################
# variabes
#######################################

SRC_DIR = src
SYSTEM_DIR = system
INCLUDE_DIR = include
LD_DIR = ldscripts
SUBMODULES_DIR = libfixmath/libfixmath

#order important
LDSCRIPT = stm32g030x6_noboot.ld
OPT = -Os
CPU_FAMILY = -mcpu=cortex-m0plus

CPU = STM32G030

HSE_VALUE = 8000000

STACK_SIZE = 512


LDSCRIPT += wbmcu_noboot_clean.ld

include libwbmcu-system/build_common.mk
