# This test name
TEST_NAME = mcu_vbat_test

# Project root directory
PROJ_DIR = ../..

# Source files to be tested
TESTED_SRC += $(PROJ_DIR)/src/mcu-vbat.c

# Unittest helpers directory
UTEST_HELPERS_DIR = ../utest_helpers

# Auxiliary source files used in tests (mocks and helpers)
AUX_SRC += $(UTEST_HELPERS_DIR)/adc/utest_adc.c
AUX_SRC += $(UTEST_HELPERS_DIR)/systick/utest_systick.c
AUX_SRC += $(UTEST_HELPERS_DIR)/regmap/utest_regmap.c
AUX_SRC += $(UTEST_HELPERS_DIR)/wbmcu_system/utest_wbmcu_system.c
AUX_SRC += $(UTEST_HELPERS_DIR)/temperature-control/utest_temperature_control.c
AUX_SRC += $(PROJ_DIR)/libfixmath/libfixmath/fix16.c

# Include directories
INC += $(UTEST_HELPERS_DIR)
INC += $(UTEST_HELPERS_DIR)/adc
INC += $(UTEST_HELPERS_DIR)/systick
INC += $(UTEST_HELPERS_DIR)/regmap
INC += $(UTEST_HELPERS_DIR)/wbmcu_system
INC += $(UTEST_HELPERS_DIR)/temperature-control
INC += $(PROJ_DIR)/include
INC += $(PROJ_DIR)/system/include
INC += $(PROJ_DIR)/libfixmath/libfixmath

# List of tests
TEST_LIST = mcu_vbat_test

# Compiler definitions
DEFS += UNITY_OUTPUT_COLOR

# Алгоритм одинаковый для обеих моделей, но конфиги разные
TARGETS_LIST = MODEL_WB74 MODEL_WB85

include $(PROJ_DIR)/system/build_unittests.mk
