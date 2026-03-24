# This test name
TEST_NAME = wbmz_subsystem_test

# Project root directory
PROJ_DIR = ../..

# Source files to be tested
TESTED_SRC += $(PROJ_DIR)/src/wbmz-subsystem.c
TESTED_SRC += $(PROJ_DIR)/src/wbmz6-supercap.c

# Unittest helpers directory
UTEST_HELPERS_DIR = ../utest_helpers

# Auxiliary source files used in tests (mocks and helpers)
AUX_SRC += $(UTEST_HELPERS_DIR)/adc/utest_adc.c
AUX_SRC += $(UTEST_HELPERS_DIR)/systick/utest_systick.c
AUX_SRC += $(UTEST_HELPERS_DIR)/regmap/utest_regmap.c
AUX_SRC += $(UTEST_HELPERS_DIR)/wbmz-common/utest_wbmz_common.c
AUX_SRC += $(UTEST_HELPERS_DIR)/wbmz6-battery/utest_wbmz6_battery.c
AUX_SRC += $(PROJ_DIR)/libfixmath/libfixmath/fix16.c

# Include directories
INC += $(UTEST_HELPERS_DIR)
INC += $(UTEST_HELPERS_DIR)/adc
INC += $(UTEST_HELPERS_DIR)/systick
INC += $(UTEST_HELPERS_DIR)/regmap
INC += $(UTEST_HELPERS_DIR)/wbmz-common
INC += $(UTEST_HELPERS_DIR)/wbmz6-battery
INC += $(PROJ_DIR)/include
INC += $(PROJ_DIR)/system/include
INC += $(PROJ_DIR)/libfixmath/libfixmath

# List of tests
TEST_LIST = wbmz_subsystem_test

# Compiler definitions
DEFS += UNITY_OUTPUT_COLOR

# List of targets (WB74 - without WBMZ6, WB85 - with WBMZ6 support)
TARGETS_LIST = MODEL_WB74 MODEL_WB85

include $(PROJ_DIR)/system/build_unittests.mk
