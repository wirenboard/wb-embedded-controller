# This test name
TEST_NAME = wbmz_common_test

# Project root directory
PROJ_DIR = ../..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/src/wbmz-common.c

# Unittest helpers directory
UTEST_HELPERS_DIR = ../utest_helpers

# Auxilary source files used in tests
AUX_SRC += $(UTEST_HELPERS_DIR)/gpio/utest_gpio.c
AUX_SRC += $(UTEST_HELPERS_DIR)/voltage-monitor/utest_voltage_monitor.c
AUX_SRC += $(UTEST_HELPERS_DIR)/systick/utest_systick.c
AUX_SRC += $(PROJ_DIR)/libfixmath/libfixmath/fix16.c

# Include directories
INC += .
INC += $(UTEST_HELPERS_DIR)
INC += $(UTEST_HELPERS_DIR)/gpio
INC += $(UTEST_HELPERS_DIR)/voltage-monitor
INC += $(UTEST_HELPERS_DIR)/systick
INC += $(PROJ_DIR)/include
INC += $(PROJ_DIR)/system/include
INC += $(PROJ_DIR)/libfixmath/libfixmath

# List of tests
TEST_LIST = wbmz_common_test

# Compiler defs
DEFS += UNITY_OUTPUT_COLOR

# List of targets
TARGETS_LIST = MODEL_WB74 MODEL_WB85

include $(PROJ_DIR)/system/build_unittests.mk
