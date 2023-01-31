# bash environment variabe

DEFS += $(MODEL_DEFINE)

#######################################
# version parsing
#######################################

# string with version
VERSION = $(shell cat ChangeLog | grep version: | head -n 1 | awk '/[ ]*(\* version:)[ ]*[0-9]+\.[0-9]+\.[0-9]+(\+wb[1-9][0-9]*|-rc[1-9][0-9]*)?$$/{print $$0}' | sed 's/.*version:[ ]*//')

# split version into digits
VERSION_MAJOR := $(shell echo $(VERSION) | sed 's/[/.].*//')
VERSION_MINOR := $(shell echo $(VERSION) | sed 's/[0-9]*[/.]//' | sed 's/[/.].*//')
VERSION_PATCH := $(shell echo $(VERSION) | sed 's/[0-9]*[/.][0-9]*[/.]//' | sed 's/[-\+].*//')
VERSION_SUFFIX := $(shell echo $(VERSION) | sed 's/[0-9]*[/.][0-9]*[/.][0-9]*//' | sed 's/[\+a-zA-Z]*//g')

ifndef VERSION_SUFFIX
VERSION_SUFFIX := 0
endif

# modify suffix to use in formula (see README.md)
ifeq ($(shell test $(VERSION_SUFFIX) -ge 0; echo $$?),0)
VERSION_SUFFIX_M := $(shell echo $$(( 128 + $(VERSION_SUFFIX) )))
else
VERSION_SUFFIX_M := $(shell echo $$(( -1 - $(VERSION_SUFFIX) )))
endif

# global defines with version in different formats
DEFS += MODBUS_DEVICE_FW_VERSION_NUMBERS=$(VERSION_MAJOR),$(VERSION_MINOR),$(VERSION_PATCH),$(VERSION_SUFFIX)
DEFS += MODBUS_DEVICE_FW_VERSION_STRING=$(shell echo $(VERSION) | sed "s/./\\\\\'&\\\\\',/g" | sed 's/.$$//')
DEFS += MODBUS_DEVICE_FW_VERSION=$(shell echo $$(( ($(VERSION_MAJOR) << 24) + ($(VERSION_MINOR) << 16) + ($(VERSION_PATCH) << 8) + $(VERSION_SUFFIX_M) )))

#######################################
# target and git info
#######################################

TARGET = $(strip $(subst MODEL_,, $(MODEL_DEFINE)))
TARGET_DIR = $(addprefix build/, $(TARGET))
BUILD_DIR = $(TARGET_DIR)/build
RELEASE_DIR = release
INFW_DIR = release_internal

GIT_HASH := $(shell git rev-parse HEAD | cut -c -7 )
GIT_BRANCH := $(shell git rev-parse --abbrev-ref HEAD | sed "s/\//_/")
GIT_INFO := $(shell echo "$(GIT_HASH)"_"$(GIT_BRANCH)" | head -c 56)
GIT_INFO := $(shell echo "\\\"$(GIT_INFO)\\\"")

TARGET_GIT_INFO := $(shell echo $(TARGET)__$(VERSION)_$(GIT_BRANCH)_$(GIT_HASH))
# TARGET_GIT_INFO := "test"

DEFS += $(CPU) HSE_VALUE=$(HSE_VALUE) MODBUS_DEVICE_GIT_INFO=$(GIT_INFO)

LD_DIR = ldscripts/
INCLUDE_DIR += system/cmsis
INCLUDE_DIR += system/include
# C_SOURCES += libwbmcu-system/startup.c

C_SOURCES += $(wildcard $(SRC_DIR)/*.c)

# add submodules
C_SOURCES += $(wildcard $(addsuffix /*.c, $(SUBMODULES_DIR)))

C_INCLUDES = $(INCLUDE_DIR) $(SUBMODULES_DIR)

ASM_SOURCES = system/startup_stm32g030xx.s

MCU = $(CPU_FAMILY) -mthumb $(FPU) $(FLOAT-ABI)

#######################################
# toolchain binaries
#######################################

# GCC_DIR = /home/nikita/software/gcc-arm-none-eabi-7-2018-q2-update/bin/
PREFIX = arm-none-eabi-

CC = $(GCC_DIR)$(PREFIX)gcc
AS = $(GCC_DIR)$(PREFIX)gcc -x assembler-with-cpp
CP = $(GCC_DIR)$(PREFIX)objcopy
SZ = $(GCC_DIR)$(PREFIX)size

HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S

#######################################
# build flags
#######################################

AS_DEFS =
AS_INCLUDES =

CC_LD_FLAGS = -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -Wall -Wextra

CFLAGS += $(MCU) $(OPT) $(addprefix -D, $(DEFS)) $(addprefix -I, $(C_INCLUDES)) $(CC_LD_FLAGS) -c -std=gnu11
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

ASFLAGS = $(MCU) $(AS_DEFS) $(AS_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections

# list of objects
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
# list of ASM program objects
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

LD_FILES += $(addprefix $(LD_DIR)/, $(LDSCRIPT))

# LDFLAGS += $(MCU) $(OPT) $(CC_LD_FLAGS) -nostartfiles -Xlinker --gc-sections -Wl,-Map,"$(BUILD_DIR)/$(TARGET).map"
# LDFLAGS +=  $(addprefix -T, $(LD_FILES)) $(TARGET_DIR)/stack_size.ld

# LDSCRIPT = ldscripts/STM32G030C6Tx_FLASH.ld
LIBS = -lc -lm -lnosys 
# LIBDIR = 
LDFLAGS = $(MCU) -specs=nano.specs $(LIBDIR) $(LIBS) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

# LDFLAGS += $(MCU) $(OPT) $(CC_LD_FLAGS) $(LIBS) -Xlinker --gc-sections -Wl,-Map,"$(BUILD_DIR)/$(TARGET).map",
LDFLAGS +=  $(addprefix -T, $(LD_FILES)) $(TARGET_DIR)/stack_size.ld


#######################################
# targets
#######################################

all: $(MODEL_LIST)

internal: $(addprefix INTERNAL_, $(MODEL_LIST))

INTERNAL_MODEL_%:
	MODEL_DEFINE=$(subst INTERNAL_,,$@) "$(MAKE)" --no-print-directory file_internal_fw
	@echo

MODEL_%:
	MODEL_DEFINE=$@ "$(MAKE)" --no-print-directory build_model
	@echo

unit_tests:
	if [ -d "unittests" ]; then cd unittests && $(MAKE); fi

file_internal_fw: $(TARGET_DIR)/$(TARGET).bin $(INFW_DIR)
	./libwbmcu-system/make_infw.sh $< $(INFW_DIR)/$(TARGET_GIT_INFO).infw

build_model: $(RELEASE_DIR) $(TARGET_DIR)/$(TARGET).elf $(TARGET_DIR)/$(TARGET).hex $(TARGET_DIR)/$(TARGET).bin
	cp $(TARGET_DIR)/$(TARGET).bin $(RELEASE_DIR)/$(TARGET_GIT_INFO).bin

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(CFLAGS) $< -o $@

$(TARGET_DIR)/$(TARGET).elf: version_check unit_tests $(OBJECTS) $(LD_FILES) $(TARGET_DIR)/stack_size.ld Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	@echo
	$(SZ) $@
	@echo

$(TARGET_DIR)/stack_size.ld:
	$(shell echo "_Minimum_Stack_Size = $(STACK_SIZE);" > $@)

$(TARGET_DIR)/%.hex: $(TARGET_DIR)/%.elf
	$(HEX) $< $@

$(TARGET_DIR)/%.bin: $(TARGET_DIR)/%.elf
	$(BIN) $< $@

$(TARGET_DIR):
	mkdir -p $@

$(BUILD_DIR): $(TARGET_DIR)
	mkdir -p $@

$(RELEASE_DIR):
	mkdir -p $@

$(INFW_DIR):
	mkdir -p $@

clean:
	rm -rf build
	rm -rf $(RELEASE_DIR) 
	rm -rf $(INFW_DIR)
	if [ -d "unittests" ]; then cd unittests && $(MAKE) clean; fi

version:
	@echo $(VERSION)

version_check:
	@if [ -z $(VERSION) ]; then echo "ERROR: No version detected in Changelog" && exit 1; fi

-include $(wildcard $(BUILD_DIR)/*.d)
