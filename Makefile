#######################################
# MODEL defines list
#######################################

MODEL_LIST = MODEL_WB74

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

# need to clear this or Debian builder will accumulate all the flags
# from the previous steps (make clean, for example) and it will be full of crap
LDFLAGS=-fno-stack-protector

include libwbmcu-system/build_common.mk

INSTALL_DIR=/usr/lib/wb-ec-firmware

install: $(TARGET_DIR)/$(TARGET).bin
	/usr/bin/install -Dm0644 $(TARGET_DIR)/$(TARGET).bin $(INSTALL_DIR)/firmware.bin
