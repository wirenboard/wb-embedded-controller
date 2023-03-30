#######################################
# MODEL defines list
#######################################

MODEL_LIST = \
MODEL_WB74

#######################################
# variabes
#######################################

SRC_DIR = src
INCLUDE_DIR = include
SUBMODULES_DIR = libfixmath/libfixmath

#order important
LDSCRIPT = stm32g030x6_noboot.ld
LDSCRIPT += wbmcu_noboot_clean.ld

OPT = -Os
CPU_FAMILY = -mcpu=cortex-m0plus

CPU = STM32G030

HSE_VALUE = 8000000

STACK_SIZE = 512

include libwbmcu-system/build_common.mk
