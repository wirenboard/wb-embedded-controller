# This test name
TEST_NAME = ntc_test

# Project root directory
PROJ_DIR = ../..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/src/ntc.c

# Unittest helpers directory
UTEST_HELPERS_DIR = ../utest_helpers

# Auxilary source files used in tests
AUX_SRC += $(PROJ_DIR)/libfixmath/libfixmath/fix16.c

# Include directories
INC += .
INC += $(UTEST_HELPERS_DIR)
INC += $(PROJ_DIR)/include
INC += $(PROJ_DIR)/system/include
INC += $(PROJ_DIR)/libfixmath/libfixmath

# List of tests
TEST_LIST = ntc_test

# Compiler defs
DEFS += UNITY_OUTPUT_COLOR

# List of targets
TARGETS_LIST = MODEL_WB74 MODEL_WB85

include $(PROJ_DIR)/system/build_unittests.mk
