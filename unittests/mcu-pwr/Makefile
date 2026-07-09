# This test name
TEST_NAME = mcu_pwr_test

# Project root directory
PROJ_DIR = ../..

# Source files to be checked (REAL mcu-pwr.c against a register-stub env)
TESTED_SRC += $(PROJ_DIR)/src/mcu-pwr.c

# Auxiliary source files used in tests
AUX_SRC += utest_mcu_reg_env.c

# Include directories.
# ВАЖНО: локальный каталог (.) идёт ПЕРВЫМ, чтобы наш register-stub
# wbmcu_system.h перекрывал реальный device-header. system/include НЕ добавляем.
# Каталог заглушек wdt-stm32 даёт declaration-only wdt-stm32.h (без IWDG).
INC += .
INC += $(PROJ_DIR)/unittests/utest_helpers/wdt-stm32
INC += $(PROJ_DIR)/include
INC += $(PROJ_DIR)/libfixmath/libfixmath

# List of tests
TEST_LIST = mcu_pwr_test

# Compiler defs
DEFS += UNITY_OUTPUT_COLOR
DEFS += MODEL_WB85

include $(PROJ_DIR)/system/build_unittests.mk
