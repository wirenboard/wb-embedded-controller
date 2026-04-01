# This file should be included in each unit test's Makefile

# This file contains the unit test build and run targets, and also for coverage measurement and report generation
# A unit test Makefile can have multiple tests and multiple build targets. In this case, each test will be built and run for each target.

# The test names are specified in the TEST_LIST variable, the name of each test must match the name of the .c test file.
# The build targets are specified in TARGETS_LIST. When building a target, the target name will be added to the compiler define.
# If multiple build targets are not required, TARGETS_LIST must be left empty.

# It is important to specify in TESTED_SRC only those source and header code files that are checked by the test. Only these files will be included in the coverage report.
# Other source code files used to build the test can be specified in AUX_SRC

# By default Unity library will be added to unit test build
# If you want to disable it please define NO_USE_UNITY = 1 in a unit test's Makefile

# Mandatory variables that must be defined in the test's Makefile:
# - TEST_NAME     - name of test / tests group
# - PROJ_DIR      - project root directory
# - TESTED_SRC    - list of source and header code files on which the test is performed (only these files will be added to the report)
# - INC           - list of include directories
# - TEST_LIST     - list of tests to be performed, the name of each test must match the name of the .c file

# Optional variables that can be defined in the test's Makefile
# - AUX_SRC             - list of auxiliary source files used to build test (these files will NOT be added to the report)
# - TARGETS_LIST        - targets list, if used, each target will be defined for the compiler in turn and each test will be executed for it
# - DEFS                - list of extra definitions for the compiler
# - GCC_BIN             - gcc compiler binary name, default: gcc
# - GCC_FLAGS           - list of gcc compiler flags
# - NO_USE_UNITY        - set to 1 to disable Unity library usage
# - COVERAGE_ROOT_DIR   - root directory for generated coverage report (used for submodules coverage metering)

# Default values for variables if they are not defined in the test's Makefile
TEST_NAME ?= Unknown_test
PROJ_DIR ?= .
COVERAGE_ROOT_DIR ?= $(PROJ_DIR)
GCC_BIN ?= gcc

# Derive GCOV_BIN from GCC_BIN if not explicitly set
# This ensures that when using gcc-15, we use gcov-15, etc.
GCOV_BIN ?= $(subst gcc,gcov,$(GCC_BIN))

# Build and coverage report directories
BUILD_DIR = build
REPORT_DIR = covr_report

# coverage_helper.sh script path
COVERAGE_HELPER = $(PROJ_DIR)/system/coverage_helper.sh

# Add mandatory GCC flags
GCC_FLAGS += --coverage -g -O0 -Wall

# Add GCC definitions for unit test compilation
DEFS += __unittest_env__

# Set filters string for gcovr, use relative to COVERAGE_ROOT_DIR paths
GCOVR_FILTERS_STR = $(foreach file,$(TESTED_SRC),-f '$(shell python3 -c "import os; print(os.path.relpath('$(file)', '$(COVERAGE_ROOT_DIR)'))")')

# Set source files list for compiler
SRC = $(TESTED_SRC) $(AUX_SRC)

# If Unity library usage not disabled add it to sources and includes for compiler
ifneq ($(NO_USE_UNITY),1)
UNITY_DIR = $(PROJ_DIR)/system/Unity
SRC += $(UNITY_DIR)/src/unity.c
INC += $(UNITY_DIR)/src
endif

# Set coverage targets list
COVERAGE_TEST_LIST = $(addprefix COVERAGE_, $(TEST_LIST))

# Set build and report directories for each separate test
TEST_BUILD_DIRS = $(foreach test,$(TEST_LIST),$(BUILD_DIR)/$(test))
TEST_REPORT_DIRS = $(foreach test,$(TEST_LIST),$(REPORT_DIR)/$(test))

ifneq ($(TARGETS_LIST),)
# If provided TARGETS_LIST, we will build and run each test for each target
MULTIPLE_TARGETS = 1
# Build directories for each target in test should be separated
TARGET_BUILD_DIRS = $(foreach test_dir,$(TEST_BUILD_DIRS),$(foreach target,$(TARGETS_LIST),$(test_dir)/$(target)))
else
# If not provided TARGETS_LIST, we will build and run each test only for one target
MULTIPLE_TARGETS = 0
# Build directories for test are the same as TEST_BUILD_DIRS
TARGET_BUILD_DIRS = $(TEST_BUILD_DIRS)
endif

# These targets are not files
.PHONY: all coverage clean remove_build_dir remove_report_dir

# Default target for make
all: info clean run

info:
	$(GCC_BIN) --version

run: $(addprefix RUN_, $(TEST_LIST))

ifeq ($(MULTIPLE_TARGETS),1)

# Multiple targets mode, each test will be built and run for each target
$(TEST_LIST): $(TARGET_BUILD_DIRS)
	@echo "\nBuilding $(TEST_NAME) test targets..."
	@for target in $(TARGETS_LIST); do \
		echo "Building target $$target" && \
		test_dir=$(BUILD_DIR)/$@/$$target && \
		test_bin=$$test_dir/$@"_"$$target && \
		$(GCC_BIN) $(addprefix -D, $$target $(DEFS)) $(addprefix -I, $(INC)) $@.c $(SRC) $(GCC_FLAGS) -o $$test_bin; \
		if [ $$? -ne 0 ]; then exit 1; fi; \
	done

RUN_%: %
	@{ \
		test_name=$(subst RUN_,,$@) && \
		echo "\n\n================= Running test $(TEST_NAME): $$test_name =================\n" && \
		for target in $(TARGETS_LIST); do \
			test_dir=$(BUILD_DIR)/$$test_name/$$target && \
			test_bin=$$test_dir/$$test_name"_"$$target && \
			echo "\n---------- Running tests for $$target target ----------\n" && \
			rm -f $$test_dir/*.gcda && \
			$$test_bin; \
			if [ $$? -ne 0 ]; then exit 1; fi; \
		done; \
		echo "\n================ Test $(TEST_NAME): $$test_name finished =================\n"; \
	}

else #ifeq ($(MULTIPLE_TARGETS),1)

# Single target mode, each test will be built only for one target
$(TEST_LIST): $(TARGET_BUILD_DIRS)
	@echo "\nBuilding $(TEST_NAME) test..."
	{ \
		test_dir=$(BUILD_DIR)/$@ && \
		test_bin=$$test_dir/$@ && \
		$(GCC_BIN) $(addprefix -D, $(DEFS)) $(addprefix -I, $(INC)) $@.c $(SRC) $(GCC_FLAGS) -o $$test_bin; \
	}

RUN_%: %
	@{ \
		test_name=$(subst RUN_,,$@) && \
		echo "\n\n================= Running test $(TEST_NAME): $$test_name =================\n" && \
		test_dir=$(BUILD_DIR)/$$test_name && \
		test_bin=$$test_dir/$$test_name && \
		rm -f $$test_dir/*.gcda && \
		$$test_bin && \
		echo "\n================ Test $(TEST_NAME): $$test_name finished =================\n"; \
	}

endif #ifeq ($(MULTIPLE_TARGETS),1)

# Coverage metering target: run all unit tests and generate coverage data and report for each test
# After that generate summary coverage report for unit-test (for all tests in TEST_LIST)
coverage: run remove_report_dir $(COVERAGE_TEST_LIST)
	@echo "\n\n================= Generating summary coverage report for $(TEST_NAME) test =================\n"
#	Print filters string for debug information
	@echo "\nGCOVR_FILTERS_STR = $(GCOVR_FILTERS_STR)"

#	Set base name for generated files
	$(eval OUT_FILES_BASE_NAME := $(REPORT_DIR)/$(TEST_NAME)_report)
#	Generate summary coverage report for unit-test
#	Usage: coverage_helper.sh --gen-ut-coverage PROJ_DIR SEARCH_DIR OUT_FILES_BASE_NAME FUNC_MERGE_MODE GCOV_EXECUTABLE [FILTERS_STR]
	$(COVERAGE_HELPER) --gen-ut-coverage $(COVERAGE_ROOT_DIR) $(BUILD_DIR) $(OUT_FILES_BASE_NAME) 'separate' '$(GCOV_BIN)' "$(GCOVR_FILTERS_STR)"
#	Print information about generated files
	@echo "\nSummary coverage data for $(TEST_NAME) test saved: $(OUT_FILES_BASE_NAME).json"
	@echo "\nSummary coverage report for $(TEST_NAME) test saved: file://$(CURDIR)/$(OUT_FILES_BASE_NAME).html\n"

# Coverage data and report generation for each test
$(COVERAGE_TEST_LIST): $(TEST_REPORT_DIRS)
	$(eval COV_TEST_NAME := $(subst COVERAGE_,,$@))
	@echo "\n\n================= Generating coverage data and report for test $(TEST_NAME): $(COV_TEST_NAME) =================\n"
#	Print filters string for debug information
	@echo "\nGCOVR_FILTERS_STR = $(GCOVR_FILTERS_STR)"

#	Set base name for generated files
	$(eval OUT_FILES_BASE_NAME := $(REPORT_DIR)/$(COV_TEST_NAME)/$(COV_TEST_NAME)_covr)
#	Generate JSON data file and .html report and also print report for unit test coverage
#	Usage: coverage_helper.sh --gen-ut-coverage PROJ_DIR SEARCH_DIR OUT_FILES_BASE_NAME FUNC_MERGE_MODE GCOV_EXECUTABLE [FILTERS_STR]
	$(COVERAGE_HELPER) --gen-ut-coverage $(COVERAGE_ROOT_DIR) $(BUILD_DIR)/$(COV_TEST_NAME) $(OUT_FILES_BASE_NAME) 'separate' '$(GCOV_BIN)' "$(GCOVR_FILTERS_STR)"
#	Print information about generated files
	@echo "\nCoverage data for $(TEST_NAME): $(COV_TEST_NAME) test saved: $(OUT_FILES_BASE_NAME).json"
	@echo "\nCoverage report for $(TEST_NAME): $(COV_TEST_NAME) test saved: file://$(CURDIR)/$(OUT_FILES_BASE_NAME).html\n"

# Create test build directories for targets
$(TARGET_BUILD_DIRS):
	@mkdir -p $@

# Create test coverage report directories
$(TEST_REPORT_DIRS):
	@mkdir -p $@

# Remove test build directory
remove_build_dir:
	rm -rf $(BUILD_DIR)

# Remove test coverage report directory
remove_report_dir:
	rm -rf $(REPORT_DIR)

# Clean test directory: remove build and report directories
clean: remove_build_dir remove_report_dir
