# bash environment variabe

DEFS += $(MODEL_DEFINE)

#######################################
# version parsing
#######################################

# get version string from ChangeLog if VERSION_STRING is not defined
VERSION_STRING ?= $(shell cat ChangeLog | grep version: | head -n 1 | sed 's/.*version:[ ]*//')

# check version string format using regexp
VERSION := $(shell echo $(VERSION_STRING) | awk '/[0-9]+\.[0-9]+\.[0-9]+(\+wb[1-9][0-9]*|-rc[1-9][0-9]*)?$$/{print $$0}')

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
DEFS += FW_VERSION_NUMBERS=$(VERSION_MAJOR),$(VERSION_MINOR),$(VERSION_PATCH),$(VERSION_SUFFIX)
DEFS += FW_VERSION_STRING=$(shell echo $(VERSION) | sed "s/./\\\\\'&\\\\\',/g" | sed 's/.$$//')
DEFS += FW_VERSION=$(shell echo $$(( ($(VERSION_MAJOR) << 24) + ($(VERSION_MINOR) << 16) + ($(VERSION_PATCH) << 8) + $(VERSION_SUFFIX_M) )))

#######################################
# target and git info
#######################################

TARGET = $(strip $(subst MODEL_,, $(MODEL_DEFINE)))
TARGET_DIR = $(addprefix build/, $(TARGET))
BUILD_DIR = $(TARGET_DIR)/build
RELEASE_DIR = release

GIT_HASH := $(shell git rev-parse HEAD | cut -c -7 )
GIT_BRANCH := $(shell git rev-parse --abbrev-ref HEAD | sed "s/\//_/")
GIT_INFO := $(shell echo "$(GIT_HASH)"_"$(GIT_BRANCH)" | head -c 56)
GIT_INFO := $(shell echo "\\\"$(GIT_INFO)\\\"")

TARGET_GIT_INFO := $(shell echo $(TARGET)__$(VERSION)_$(GIT_BRANCH)_$(GIT_HASH))

DEFS += $(CPU) HSE_VALUE=$(HSE_VALUE) MODBUS_DEVICE_GIT_INFO=$(GIT_INFO)

LD_DIR ?= ldscripts
INCLUDE_DIR += system/cmsis
INCLUDE_DIR += system/include
C_SOURCES += system/startup.c

C_SOURCES += $(wildcard $(SRC_DIR)/*.c)

# add submodules
C_SOURCES += $(wildcard $(addsuffix /*.c, $(SUBMODULES_DIR)))

C_INCLUDES = $(INCLUDE_DIR) $(SUBMODULES_DIR)

MCU = $(CPU_FAMILY) -mthumb $(FPU) $(FLOAT-ABI)

#######################################
# toolchain binaries
#######################################

PREFIX = arm-none-eabi-

CC = $(GCC_DIR)$(PREFIX)gcc
CP = $(GCC_DIR)$(PREFIX)objcopy
SZ = $(GCC_DIR)$(PREFIX)size

HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S

#######################################
# build flags
#######################################

CC_LD_FLAGS = -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -ffreestanding -Wall -Wextra

CFLAGS += $(MCU) $(OPT) $(addprefix -D, $(DEFS)) $(addprefix -I, $(C_INCLUDES)) $(CC_LD_FLAGS) -c -std=gnu11
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))

LD_FILES += $(addprefix $(LD_DIR)/, $(LDSCRIPT))

LDFLAGS += $(MCU) $(OPT) $(CC_LD_FLAGS) -nostartfiles -Xlinker --gc-sections -Wl,-Map,"$(BUILD_DIR)/$(TARGET).map"
LDFLAGS +=  $(addprefix -T, $(LD_FILES)) $(TARGET_DIR)/stack_size.ld
LDFLAGS += -Wl,--print-memory-usage

#######################################
# unittests
#######################################

# Не запускаем тесты libfixmath, т.к. мы не меняем этот репозиторий
UNITTESTS_DIRS += $(shell find -type d | grep unittests | grep -v libfixmath)
UNITTESTS_TARGETS = $(addprefix UNITTEST_, $(UNITTESTS_DIRS))

#######################################
# targets
#######################################

all: $(MODEL_LIST)

MODEL_%: unittests
	MODEL_DEFINE=$@ "$(MAKE)" --no-print-directory build_model
	@echo

$(UNITTESTS_TARGETS):
	$(eval UT_DIR := $(subst UNITTEST_,,$@))
	@if [ -f $(UT_DIR)/Makefile ]; then \
		cd $(UT_DIR) && $(MAKE) && cd -; \
	fi

unittests: $(UNITTESTS_TARGETS)

build_model: $(RELEASE_DIR) $(TARGET_DIR)/$(TARGET).elf $(TARGET_DIR)/$(TARGET).hex $(TARGET_DIR)/$(TARGET).bin
	cp $(TARGET_DIR)/$(TARGET).bin $(RELEASE_DIR)/$(TARGET_GIT_INFO).bin

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(TARGET_DIR)/$(TARGET).elf: version_check $(OBJECTS) $(LD_FILES) $(TARGET_DIR)/stack_size.ld Makefile
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

clean:
	rm -rf build
	rm -rf $(RELEASE_DIR)
	@for dir in $(UNITTESTS_DIRS); do \
		if [ -f  $$dir/Makefile ]; then \
			cd $$dir && $(MAKE) clean --no-print-directory; cd -; \
		fi; \
	done

version:
	@echo $(VERSION)

version_check:
	@if [ -z $(VERSION) ]; then echo "ERROR: No version detected in Changelog" && exit 1; fi

-include $(wildcard $(BUILD_DIR)/*.d)
