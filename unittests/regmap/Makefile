# This test name
TEST_NAME = regmap_test

# Project root directory
PROJ_DIR = ../..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/src/regmap.c

# Include directories
INC += .
INC += ../utest_helpers
INC += $(PROJ_DIR)/include
INC += $(PROJ_DIR)/system/include

# List of tests
TEST_LIST = regmap_test

# This test does not use Unity framework
NO_USE_UNITY = 1

include $(PROJ_DIR)/system/build_unittests.mk
