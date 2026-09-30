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

# Set source files list for compiler.
# TESTED_SRC may list headers - a header-only module is tested that way, and the
# gcovr filter above is built from the same list. The compiler must not get them:
# gcc takes a .h on the command line for a main file, warns "#pragma once in main
# file" and builds a precompiled header nobody uses. Coverage is unaffected, it
# comes from the translation unit that includes the header.
SRC = $(filter-out %.h,$(TESTED_SRC)) $(AUX_SRC)

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
.PHONY: all coverage clean remove_build_dir remove_report_dir check_coverage_data

# Build the prerequisites of `all` and `coverage` in order, and everything below them in parallel under `make -jN`.
# These targets mix destructive steps with build steps as ordered prerequisites: `all: clean run`
# (clean = `rm -rf build` vs run = compile into build/) and `coverage: run remove_report_dir ...`. In parallel the
# `rm -rf` races with the compile (e.g. "cannot open build/<test>/<test>.gcno"). The tests of `run` and the targets
# of a test are independent: every test binary gets its own build directory with its .gcno and .gcda files.
# The coverage reports of the tests stay serial, as before: parallel gcovr runs over the same sources may overwrite
# each other's .gcov files.
# With targets as prerequisites .NOTPARALLEL needs GNU make 4.4. An older make ignores them and runs the whole
# Makefile serially, as before.
.NOTPARALLEL: all coverage

# Default target for make
all: clean run

run: $(addprefix RUN_, $(TEST_LIST))

ifeq ($(MULTIPLE_TARGETS),1)

# Multiple targets mode, each test will be built and run for each target.
# Every test and target pair has its own build and run targets, BUILD_<test>__<target> and RUN_<test>__<target>,
# so under -j the pairs are built and run in parallel. <test> and RUN_<test> build and run all targets of the test.
TEST_TARGET_PAIRS = $(foreach test,$(TEST_LIST),$(addprefix $(test)__,$(TARGETS_LIST)))

# Phony: with no recipe of their own, <test> would otherwise get the built-in rule "%: %.c"
.PHONY: $(TEST_LIST) $(addprefix RUN_, $(TEST_LIST)) $(addprefix BUILD_, $(TEST_TARGET_PAIRS)) $(addprefix RUN_, $(TEST_TARGET_PAIRS))

$(TEST_LIST): %: $(addprefix BUILD_%__, $(TARGETS_LIST))

$(addprefix RUN_, $(TEST_LIST)): RUN_%: $(addprefix RUN_%__, $(TARGETS_LIST))
	@echo "\n================ Test $(TEST_NAME): $* finished =================\n"

# Build and run rules of one test and target pair: $(1) - test, $(2) - target
define TEST_TARGET_RULES
BUILD_$(1)__$(2): $(BUILD_DIR)/$(1)/$(2)
	@echo "\nBuilding $$(TEST_NAME) test $(1) for target $(2)..."
	@$$(GCC_BIN) $$(addprefix -D, $(2) $$(DEFS)) $$(addprefix -I, $$(INC)) $(1).c $$(SRC) $$(GCC_FLAGS) -o $(BUILD_DIR)/$(1)/$(2)/$(1)_$(2)

RUN_$(1)__$(2): BUILD_$(1)__$(2)
	@echo "\n\n================= Running test $$(TEST_NAME): $(1) for $(2) target =================\n"
	@rm -f $(BUILD_DIR)/$(1)/$(2)/*.gcda
	@$(BUILD_DIR)/$(1)/$(2)/$(1)_$(2)
endef

$(foreach test,$(TEST_LIST),$(foreach target,$(TARGETS_LIST),$(eval $(call TEST_TARGET_RULES,$(test),$(target)))))

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

# COVERAGE_SKIP_RUN=1 (command line or environment): don't rebuild and run the tests again, take the coverage data
# left by their last run. The firmware build already runs all unit tests (MODEL_% depends on unittests), so CI
# collects coverage after it this way instead of running the tests twice
ifeq ($(COVERAGE_SKIP_RUN),1)
COVERAGE_RUN = check_coverage_data
else
COVERAGE_RUN = run
endif

# COVERAGE_DATA_ONLY=1: generate only the JSON coverage data of the tests, without the reports of the tests (.html,
# Cobertura .xml, lcov.info) and the summary report of this Makefile. The report of the project or submodule is built
# from the data of the tests only, so coverage_helper.sh --make-coverage passes it
ifeq ($(COVERAGE_DATA_ONLY),1)
COVERAGE_GEN_MODE = --gen-ut-coverage-data
else
COVERAGE_GEN_MODE = --gen-ut-coverage
endif

# Coverage metering target: run all unit tests and generate coverage data and report for each test
# After that generate summary coverage report for unit-test (for all tests in TEST_LIST), not with COVERAGE_DATA_ONLY=1
coverage: $(COVERAGE_RUN) remove_report_dir $(COVERAGE_TEST_LIST)
ifneq ($(COVERAGE_DATA_ONLY),1)
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
endif

# Coverage data and report generation for each test (only the data with COVERAGE_DATA_ONLY=1)
$(COVERAGE_TEST_LIST): $(TEST_REPORT_DIRS)
	$(eval COV_TEST_NAME := $(subst COVERAGE_,,$@))
	@echo "\n\n================= Generating coverage data and report for test $(TEST_NAME): $(COV_TEST_NAME) =================\n"
#	Print filters string for debug information
	@echo "\nGCOVR_FILTERS_STR = $(GCOVR_FILTERS_STR)"

#	Set base name for generated files
	$(eval OUT_FILES_BASE_NAME := $(REPORT_DIR)/$(COV_TEST_NAME)/$(COV_TEST_NAME)_covr)
#	Generate JSON data file and .html report and also print report for unit test coverage
#	Usage: coverage_helper.sh --gen-ut-coverage PROJ_DIR SEARCH_DIR OUT_FILES_BASE_NAME FUNC_MERGE_MODE GCOV_EXECUTABLE [FILTERS_STR]
	$(COVERAGE_HELPER) $(COVERAGE_GEN_MODE) $(COVERAGE_ROOT_DIR) $(BUILD_DIR)/$(COV_TEST_NAME) $(OUT_FILES_BASE_NAME) 'separate' '$(GCOV_BIN)' "$(GCOVR_FILTERS_STR)"
#	Print information about generated files
	@echo "\nCoverage data for $(TEST_NAME): $(COV_TEST_NAME) test saved: $(OUT_FILES_BASE_NAME).json"
ifneq ($(COVERAGE_DATA_ONLY),1)
	@echo "\nCoverage report for $(TEST_NAME): $(COV_TEST_NAME) test saved: file://$(CURDIR)/$(OUT_FILES_BASE_NAME).html\n"
endif

# Fail instead of reporting zero coverage when COVERAGE_SKIP_RUN=1 is given, but the tests have not been run
check_coverage_data:
	@find $(BUILD_DIR) -name '*.gcda' 2>/dev/null | grep -q . || \
		{ echo "No coverage data in $(CURDIR)/$(BUILD_DIR): run the tests first or drop COVERAGE_SKIP_RUN=1"; exit 1; }

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
