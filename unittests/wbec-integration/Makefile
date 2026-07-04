# This test name
TEST_NAME = wbec_integration_test

# Project root directory
PROJ_DIR = ../..

# Source files to be checked
# Интеграционный тест: реальные wbec + linux-power-control + wdt работают вместе,
# как в main loop прошивки. Проверяется взаимодействие автоматов состояний.
TESTED_SRC += $(PROJ_DIR)/src/wbec.c
TESTED_SRC += $(PROJ_DIR)/src/linux-power-control.c
TESTED_SRC += $(PROJ_DIR)/src/wdt.c

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
AUX_SRC += $(UTEST_HELPERS_DIR)/gpio/utest_gpio.c

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
INC += $(UTEST_HELPERS_DIR)/wbmz-common
INC += $(UTEST_HELPERS_DIR)/rtc
INC += $(UTEST_HELPERS_DIR)/irq
INC += $(UTEST_HELPERS_DIR)/pwrkey
INC += $(UTEST_HELPERS_DIR)/gpio
INC += $(PROJ_DIR)/include
INC += $(PROJ_DIR)/system/include
INC += $(PROJ_DIR)/libfixmath/libfixmath

# List of tests
TEST_LIST = wbec_integration_test

# Compiler defs
DEFS += UNITY_OUTPUT_COLOR
DEFS += "FW_VERSION_STRING='t','e','s','t'"
DEFS += "MODBUS_DEVICE_GIT_INFO=\"test\""

# List of targets
TARGETS_LIST = MODEL_WB74 MODEL_WB85

include $(PROJ_DIR)/system/build_unittests.mk
