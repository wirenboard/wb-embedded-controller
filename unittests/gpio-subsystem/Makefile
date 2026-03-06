# This test name
TEST_NAME = gpio_subsystem_test

# Project root directory
PROJ_DIR = ../..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/src/gpio-subsytem.c

# Unittest helpers directory
UTEST_HELPERS_DIR = ../utest_helpers

# Auxilary source files used in tests
AUX_SRC += $(UTEST_HELPERS_DIR)/gpio/utest_gpio.c
AUX_SRC += $(UTEST_HELPERS_DIR)/shared-gpio/utest_shared_gpio.c
AUX_SRC += $(UTEST_HELPERS_DIR)/regmap/utest_regmap.c
AUX_SRC += $(UTEST_HELPERS_DIR)/voltage-monitor/utest_voltage_monitor.c

# Include directories
INC += .
INC += $(UTEST_HELPERS_DIR)
INC += $(UTEST_HELPERS_DIR)/gpio
INC += $(UTEST_HELPERS_DIR)/shared-gpio
INC += $(UTEST_HELPERS_DIR)/regmap
INC += $(UTEST_HELPERS_DIR)/voltage-monitor
INC += $(PROJ_DIR)/include
INC += $(PROJ_DIR)/system/include

# List of tests
TEST_LIST = gpio_subsystem_test

# Compiler defs
DEFS += UNITY_OUTPUT_COLOR

# List of targets
TARGETS_LIST = MODEL_WB74 MODEL_WB85

include $(PROJ_DIR)/system/build_unittests.mk
