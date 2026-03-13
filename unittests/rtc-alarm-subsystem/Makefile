# This test name
TEST_NAME = rtc_alarm_subsystem_test

# Project root directory
PROJ_DIR = ../..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/src/rtc-alarm-subsystem.c

# Unittest helpers directory
UTEST_HELPERS_DIR = ../utest_helpers

# Auxilary source files used in tests
AUX_SRC += $(UTEST_HELPERS_DIR)/regmap/utest_regmap.c
AUX_SRC += $(UTEST_HELPERS_DIR)/rtc/utest_rtc.c
AUX_SRC += $(UTEST_HELPERS_DIR)/irq/utest_irq.c

# Include directories
INC += .
INC += $(UTEST_HELPERS_DIR)
INC += $(UTEST_HELPERS_DIR)/regmap
INC += $(UTEST_HELPERS_DIR)/rtc
INC += $(UTEST_HELPERS_DIR)/irq
INC += $(PROJ_DIR)/include
INC += $(PROJ_DIR)/system/include

# List of tests
TEST_LIST = rtc_alarm_subsystem_test

# Compiler defs
DEFS += UNITY_OUTPUT_COLOR

include $(PROJ_DIR)/system/build_unittests.mk
