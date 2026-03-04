# This test name
TEST_NAME = temperature_control_test

# Project root directory
PROJ_DIR = ../..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/src/temperature-control.c

# Unittest helpers directory
UTEST_HELPERS_DIR = ../utest_helpers

# Auxilary source files used in tests
AUX_SRC += $(UTEST_HELPERS_DIR)/adc/utest_adc.c
AUX_SRC += $(UTEST_HELPERS_DIR)/ntc/utest_ntc.c
AUX_SRC += $(UTEST_HELPERS_DIR)/gpio/utest_gpio.c
AUX_SRC += $(UTEST_HELPERS_DIR)/wbmz-common/utest_wbmz_common.c
AUX_SRC += $(UTEST_HELPERS_DIR)/voltage-monitor/utest_voltage_monitor.c
AUX_SRC += $(PROJ_DIR)/libfixmath/libfixmath/fix16.c

# Include directories
INC += .
INC += $(UTEST_HELPERS_DIR)
INC += $(UTEST_HELPERS_DIR)/adc
INC += $(UTEST_HELPERS_DIR)/ntc
INC += $(UTEST_HELPERS_DIR)/gpio
INC += $(UTEST_HELPERS_DIR)/wbmz-common
INC += $(UTEST_HELPERS_DIR)/voltage-monitor
INC += $(PROJ_DIR)/include
INC += $(PROJ_DIR)/system/include
INC += $(PROJ_DIR)/libfixmath/libfixmath

# List of tests
TEST_LIST = temperature_control_test

# Compiler defs
DEFS += UNITY_OUTPUT_COLOR MODEL_WB85

include $(PROJ_DIR)/system/build_unittests.mk
