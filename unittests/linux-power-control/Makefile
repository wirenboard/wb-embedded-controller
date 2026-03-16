# This test name
TEST_NAME = linux_power_control_test

# Project root directory
PROJ_DIR = ../..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/src/linux-power-control.c

# Unittest helpers directory
UTEST_HELPERS_DIR = ../utest_helpers

# Auxiliary source files used in tests
AUX_SRC += $(UTEST_HELPERS_DIR)/mcu-pwr/utest_mcu_pwr.c
AUX_SRC += $(UTEST_HELPERS_DIR)/systick/utest_systick.c
AUX_SRC += $(UTEST_HELPERS_DIR)/voltage-monitor/utest_voltage_monitor.c
AUX_SRC += $(UTEST_HELPERS_DIR)/gpio/utest_gpio.c
AUX_SRC += $(UTEST_HELPERS_DIR)/wbmcu_system/utest_wbmcu_system.c
AUX_SRC += $(UTEST_HELPERS_DIR)/wdt-stm32/utest_wdt_stm32.c
AUX_SRC += $(UTEST_HELPERS_DIR)/system-led/utest_system_led.c
AUX_SRC += $(UTEST_HELPERS_DIR)/console/utest_console.c
AUX_SRC += $(UTEST_HELPERS_DIR)/pwrkey/utest_pwrkey.c
AUX_SRC += $(UTEST_HELPERS_DIR)/wbmz-common/utest_wbmz_common.c

# Include directories
INC += $(UTEST_HELPERS_DIR)
INC += $(UTEST_HELPERS_DIR)/mcu-pwr
INC += $(UTEST_HELPERS_DIR)/systick
INC += $(UTEST_HELPERS_DIR)/voltage-monitor
INC += $(UTEST_HELPERS_DIR)/gpio
INC += $(UTEST_HELPERS_DIR)/wbmcu_system
INC += $(UTEST_HELPERS_DIR)/wdt-stm32
INC += $(UTEST_HELPERS_DIR)/pwrkey
INC += $(UTEST_HELPERS_DIR)/system-led
INC += $(UTEST_HELPERS_DIR)/wbmz-common
INC += $(PROJ_DIR)/include
INC += $(PROJ_DIR)/system/include
INC += $(PROJ_DIR)/libfixmath/libfixmath

# List of tests
TEST_LIST = linux_power_control_test

# Compiler defs
DEFS += UNITY_OUTPUT_COLOR

# List of targets
TARGETS_LIST = MODEL_WB85

include $(PROJ_DIR)/system/build_unittests.mk
