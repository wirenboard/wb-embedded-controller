# This test name
TEST_NAME = wbec_test

# Project root directory
PROJ_DIR = ../..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/src/wbec.c

# Unittest helpers directory
UTEST_HELPERS_DIR = ../utest_helpers

# Auxiliary source files used in tests
AUX_SRC += $(UTEST_HELPERS_DIR)/adc/utest_adc.c
AUX_SRC += $(UTEST_HELPERS_DIR)/systick/utest_systick.c
AUX_SRC += $(UTEST_HELPERS_DIR)/voltage-monitor/utest_voltage_monitor.c
AUX_SRC += $(UTEST_HELPERS_DIR)/mcu-pwr/utest_mcu_pwr.c
AUX_SRC += $(UTEST_HELPERS_DIR)/regmap/utest_regmap.c
AUX_SRC += $(UTEST_HELPERS_DIR)/system-led/utest_system_led.c
AUX_SRC += $(UTEST_HELPERS_DIR)/wbmcu_system/utest_wbmcu_system.c
AUX_SRC += $(UTEST_HELPERS_DIR)/wdt-stm32/utest_wdt_stm32.c
AUX_SRC += $(UTEST_HELPERS_DIR)/wbmz-common/utest_wbmz_common.c
AUX_SRC += $(UTEST_HELPERS_DIR)/rtc/utest_rtc.c
AUX_SRC += $(UTEST_HELPERS_DIR)/irq/utest_irq.c
AUX_SRC += $(UTEST_HELPERS_DIR)/pwrkey/utest_pwrkey.c
AUX_SRC += $(UTEST_HELPERS_DIR)/console/utest_console.c
AUX_SRC += $(UTEST_HELPERS_DIR)/wdt/utest_wdt.c
AUX_SRC += ./wbec_test_stubs.c

# Include directories
INC += .
INC += $(UTEST_HELPERS_DIR)
INC += $(UTEST_HELPERS_DIR)/adc
INC += $(UTEST_HELPERS_DIR)/systick
INC += $(UTEST_HELPERS_DIR)/voltage-monitor
INC += $(UTEST_HELPERS_DIR)/mcu-pwr
INC += $(UTEST_HELPERS_DIR)/regmap
INC += $(UTEST_HELPERS_DIR)/system-led
INC += $(UTEST_HELPERS_DIR)/wbmcu_system
INC += $(UTEST_HELPERS_DIR)/wdt-stm32
INC += $(UTEST_HELPERS_DIR)/wdt
INC += $(UTEST_HELPERS_DIR)/wbmz-common
INC += $(UTEST_HELPERS_DIR)/rtc
INC += $(UTEST_HELPERS_DIR)/irq
INC += $(UTEST_HELPERS_DIR)/pwrkey
INC += $(PROJ_DIR)/include
INC += $(PROJ_DIR)/system/include
INC += $(PROJ_DIR)/libfixmath/libfixmath

# List of tests
TEST_LIST = wbec_test

# Compiler defs
DEFS += UNITY_OUTPUT_COLOR
DEFS += "FW_VERSION_STRING='t','e','s','t'"
DEFS += "MODBUS_DEVICE_GIT_INFO=\"test\""

# List of targets
TARGETS_LIST = MODEL_WB74 MODEL_WB85

include $(PROJ_DIR)/system/build_unittests.mk
