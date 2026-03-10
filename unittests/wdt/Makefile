# This test name
TEST_NAME = wdt_test

# Project root directory
PROJ_DIR = ../..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/src/wdt.c

# Unittest helpers directory
UTEST_HELPERS_DIR = ../utest_helpers

# Auxilary source files used in tests
AUX_SRC += $(UTEST_HELPERS_DIR)/systick/utest_systick.c
AUX_SRC += $(UTEST_HELPERS_DIR)/regmap/utest_regmap.c

# Include directories
INC += .
INC += $(UTEST_HELPERS_DIR)
INC += $(UTEST_HELPERS_DIR)/systick
INC += $(UTEST_HELPERS_DIR)/regmap
INC += $(PROJ_DIR)/include
INC += $(PROJ_DIR)/system/include

# List of tests
TEST_LIST = wdt_test

# Compiler defs
DEFS += UNITY_OUTPUT_COLOR MODEL_WB74

include $(PROJ_DIR)/system/build_unittests.mk
