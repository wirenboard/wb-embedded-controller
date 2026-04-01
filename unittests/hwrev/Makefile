# This test name
TEST_NAME = hwrev_test

# Project root directory
PROJ_DIR = ../..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/src/hwrev.c

# Unittest helpers directory
UTEST_HELPERS_DIR = ../utest_helpers

# Auxilary source files used in tests
AUX_SRC += $(UTEST_HELPERS_DIR)/adc/utest_adc.c
AUX_SRC += $(UTEST_HELPERS_DIR)/regmap/utest_regmap.c
AUX_SRC += $(UTEST_HELPERS_DIR)/systick/utest_systick.c
AUX_SRC += $(UTEST_HELPERS_DIR)/system-led/utest_system_led.c
AUX_SRC += $(UTEST_HELPERS_DIR)/wdt-stm32/utest_wdt_stm32.c
AUX_SRC += $(UTEST_HELPERS_DIR)/wbmcu_system/utest_wbmcu_system.c
AUX_SRC += $(UTEST_HELPERS_DIR)/mcu-pwr/utest_mcu_pwr.c
AUX_SRC += $(PROJ_DIR)/libfixmath/libfixmath/fix16.c
AUX_SRC += hwrev_test_stubs.c

# Include directories
INC += .
INC += $(UTEST_HELPERS_DIR)
INC += $(UTEST_HELPERS_DIR)/adc
INC += $(UTEST_HELPERS_DIR)/regmap
INC += $(UTEST_HELPERS_DIR)/systick
INC += $(UTEST_HELPERS_DIR)/system-led
INC += $(UTEST_HELPERS_DIR)/wdt-stm32
INC += $(UTEST_HELPERS_DIR)/wbmcu_system
INC += $(UTEST_HELPERS_DIR)/mcu-pwr
INC += $(PROJ_DIR)/include
INC += $(PROJ_DIR)/system/include
INC += $(PROJ_DIR)/libfixmath/libfixmath

# List of tests
TEST_LIST = hwrev_test

# Compiler defs
DEFS += UNITY_OUTPUT_COLOR
DEFS += FW_VERSION_NUMBERS='2,3,5,7'

# List of targets
TARGETS_LIST = MODEL_WB74 MODEL_WB85

include $(PROJ_DIR)/system/build_unittests.mk
