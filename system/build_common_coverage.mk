# This file is included in build_common.mk which is included in the project's Makefile

# This file contains the "coverage" build target for measuring the coverage of the project's unit tests.
# The gcovr utility is used to measure test coverage.
# The project is searched for unittests directories and nested subdirectories containing unit test Makefiles,
# and coverage measurement is run for each unit test, which generates JSON files with coverage data for individual tests.
# Then, for every model in MODEL_LIST, it takes the code lines of the model firmware from the compiler (see "Code lines
# of a model firmware" below) and merges them into the report with zero hits: files without unit tests are counted by
# their code lines only, and code the unit tests don't build (#ifdef branches of other models) shows up as uncovered.
# Finally, it generates an .html report on the project's coverage by tests, and also prints the report to the console.
# In the generated .html report, you can clearly see which lines of code are covered by tests and which are not.
# Alongside the .html report, a Cobertura .xml report (coverage.xml) is generated: CI builds the PR coverage comment from it.

# Optional variables that can be assigned in a project Makefile or build_common.mk
# - COVERAGE_NO_ADD_UNCOVERED_FILES     Set to 1 to report only the code the unit tests build: no files without tests,
#                                       no code of other models
# - COVERAGE_FAIL_UNDER                 Set to <theshold> value [%] if you want to gcovr fails when <project coverage> < <theshold>
# - COVERAGE_NOTES_FLAGS                Extra compiler flags for the firmware code lines of the models, put before the
#                                       project's defines and includes: e.g. -I of a directory with a stand-in for a header
#                                       only the firmware build generates (see "Code lines of a model firmware" below)

# Optional variables that can be given on the command line or in the environment
# - COVERAGE_SKIP_RUN                   Set to 1 to collect coverage from the last run of the unit tests instead of rebuilding
#                                       and running them again


#######################################
# Coverage
#######################################

# Sources list for coverage measurement
COVERAGE_C_SOURCES = $(foreach f,$(C_SOURCES),$(if $(strip \
	$(foreach pattern,$(DISABLE_UNITTESTS),$(findstring $(pattern),$(f)))),,$(f)) \
)

# Sources list for project-only coverage measurement:
# filter out SUBMODULE_C_SOURCES and also any sources from DISABLE_UNITTESTS
PROJECT_C_SOURCES = $(foreach f,$(filter-out $(SUBMODULE_C_SOURCES), $(C_SOURCES)),$(if $(strip \
	$(foreach pattern,$(DISABLE_UNITTESTS),$(findstring $(pattern),$(f)))),,$(f)) \
)

# Automatically extract TESTED_SRC from all unittest Makefiles
# This finds header files (.h) specified as TESTED_SRC in unittest Makefiles
# and normalizes the paths (removes PROJ_DIR prefix like '../..')
COVERAGE_H_SOURCES = $(shell \
	for dir in $(UNITTESTS_DIRS); do \
		if [ -f "$$dir/Makefile" ]; then \
			$(GREP_CMD) -E '^\s*TESTED_SRC\s*\+?=' "$$dir/Makefile" 2>/dev/null | \
			$(SED_CMD) 's/.*TESTED_SRC[[:space:]]*+\?=[[:space:]]*//' | \
			$(SED_CMD) 's/\$$(PROJ_DIR)\///g' | \
			tr ' ' '\n' | \
			$(GREP_CMD) '\.h$$' || true; \
		fi; \
	done | sort -u \
)

# All sources for coverage measurement (C files + H files specified as TESTED_SRC)
COVERAGE_ALL_SOURCES = $(COVERAGE_C_SOURCES) $(COVERAGE_H_SOURCES)

PROJECT_COVERAGE_H_SOURCES = $(shell \
	for dir in $(PROJECT_UNITTESTS_DIRS); do \
		if [ -f "$$dir/Makefile" ]; then \
			$(GREP_CMD) -E '^\s*TESTED_SRC\s*\+?=' "$$dir/Makefile" 2>/dev/null | \
			$(SED_CMD) 's/.*TESTED_SRC[[:space:]]*+\?=[[:space:]]*//' | \
			$(SED_CMD) 's/\$$(PROJ_DIR)\///g' | \
			tr ' ' '\n' | \
			$(GREP_CMD) '\.h$$' || true; \
		fi; \
	done | sort -u \
)

# All sources for project-only coverage measurement
PROJECT_COVERAGE_ALL_SOURCES = $(PROJECT_C_SOURCES) $(PROJECT_COVERAGE_H_SOURCES)

# Output coverage report directory and report file
COVERAGE_REPORT_DIR = covr_report
COVERAGE_REPORT_FILE = $(COVERAGE_REPORT_DIR)/coverage_report.html
COVERAGE_XML_REPORT_FILE = $(COVERAGE_REPORT_DIR)/coverage.xml

# Project-only coverage report directory and file
PROJECT_COVERAGE_REPORT_DIR = covr_report_project
PROJECT_COVERAGE_REPORT_FILE = $(PROJECT_COVERAGE_REPORT_DIR)/coverage_report.html
PROJECT_COVERAGE_XML_REPORT_FILE = $(PROJECT_COVERAGE_REPORT_DIR)/coverage.xml

# Auxiliary files used for coverage report generation
COVERAGE_DATA_LIST_FILE = $(COVERAGE_REPORT_DIR)/covr_data_list.txt
UNCOVERED_SRC_LIST_FILE = $(COVERAGE_REPORT_DIR)/uncovr_src_list.txt

# Auxiliary files for project-only coverage
PROJECT_COVERAGE_DATA_LIST_FILE = $(PROJECT_COVERAGE_REPORT_DIR)/covr_data_list.txt
PROJECT_UNCOVERED_SRC_LIST_FILE = $(PROJECT_COVERAGE_REPORT_DIR)/uncovr_src_list.txt

# Code lines of every model firmware: compiler notes in a directory per model in MODEL_LIST, read into one gcovr JSON
COVERAGE_MODEL_DIRS = $(patsubst %,$(COVERAGE_REPORT_DIR)/models/%,$(MODEL_LIST))
COVERAGE_MODELS_JSON = $(COVERAGE_REPORT_DIR)/models.json
PROJECT_COVERAGE_MODEL_DIRS = $(patsubst %,$(PROJECT_COVERAGE_REPORT_DIR)/models/%,$(MODEL_LIST))
PROJECT_COVERAGE_MODELS_JSON = $(PROJECT_COVERAGE_REPORT_DIR)/models.json

# Host compiler and gcov for the firmware code lines - the same the unit tests use (on macOS see below)
COVERAGE_GCC_BIN = $(or $(GCC_BIN),gcc)
COVERAGE_GCOV_BIN = $(subst gcc,gcov,$(COVERAGE_GCC_BIN))

# Filters string for gcovr used for coverage report generation
COVERAGE_FILTERS_STR = $(foreach file,$(COVERAGE_ALL_SOURCES),-f '$(file)')

# Filters string for project-only coverage
PROJECT_COVERAGE_FILTERS_STR = $(foreach file,$(PROJECT_COVERAGE_ALL_SOURCES),-f '$(file)')

# If COVERAGE_FAIL_UNDER value is assigned, add extra flag to gcovr for failure when (project_coverage < COVERAGE_FAIL_UNDER)
ifneq ($(COVERAGE_FAIL_UNDER),)
	COVERAGE_EXTRA_FLAGS += --fail-under-line $(COVERAGE_FAIL_UNDER)
endif

# Functions merge mode for gcovr
# We use "separate" because one function could be implemented for different targets by different ways
COVERAGE_FUNC_MERGE_MODE = --merge-mode-functions=separate

# Coverage targets, $(UNITTESTS_DIRS) is provided by build_common.mk
COVERAGE_TARGETS = $(addprefix COVERAGE_, $(UNITTESTS_DIRS))

# Project-only coverage targets, $(PROJECT_UNITTESTS_DIRS) is provided by build_common.mk
PROJECT_COVERAGE_TARGETS = $(addprefix PROJECT_COVERAGE_, $(PROJECT_UNITTESTS_DIRS))

# Dependencies list for "coverage" target
# NOTE: the report directory is no longer listed here directly. Wiping and
# recreating it is done by `prepare_coverage_dir` (see below), which the files
# written into the report dir take as a normal (not order-only) prerequisite,
# so that under `make -jN` the rm/mkdir always completes before any job touches
# the directory. Listing remove_report_dir + mkdir as plain siblings of the
# coverage jobs let them run concurrently and raced (e.g.
# "touch: cannot touch 'covr_report/covr_data_list.txt': No such file ...").
COVERAGE_DEPS = $(COVERAGE_TARGETS)

# Dependencies list for "coverage-project" target
PROJECT_COVERAGE_DEPS = $(PROJECT_COVERAGE_TARGETS)

# Add trace files string for gcovr used in "coverage" target
# JSON_LIST is assigned inside the "coverage" target
COVERAGE_ADD_TRACE_FILES = $(addprefix -a , $(JSON_LIST))

# Add trace files string for gcovr used in "coverage-project" target
# PROJECT_JSON_LIST is assigned inside the "coverage-project" target
PROJECT_COVERAGE_ADD_TRACE_FILES = $(addprefix -a , $(PROJECT_JSON_LIST))

#######################################
# Coverage targets
#######################################

ifneq ($(COVERAGE_NO_ADD_UNCOVERED_FILES),1) # Addition of the firmware code lines to the report is enabled

# Add the firmware code lines of every model to dependencies for "coverage" target and to add trace files string for gcovr
COVERAGE_DEPS += $(UNCOVERED_SRC_LIST_FILE) $(COVERAGE_MODELS_JSON)
COVERAGE_ADD_TRACE_FILES += -a $(COVERAGE_MODELS_JSON)

# Generate a data file with the code lines of all model firmwares (see "Code lines of a model firmware" below)
$(COVERAGE_MODELS_JSON): $(COVERAGE_MODEL_DIRS) $(COVERAGE_TARGETS)
	@gcovr -r . --gcov-executable $(COVERAGE_GCOV_BIN) $(COVERAGE_FUNC_MERGE_MODE) --json -o $@ $(COVERAGE_REPORT_DIR)/models
	@echo "Firmware code lines of $(words $(MODEL_LIST)) models saved: $@"

# Compile the sources for the notes of one model
$(COVERAGE_MODEL_DIRS): $(COVERAGE_REPORT_DIR)/models/%: prepare_coverage_dir
	@MODEL_DEFINE=$* "$(MAKE)" --no-print-directory coverage_model_notes \
		COVERAGE_MODEL_DIR=$@ COVERAGE_MODEL_SOURCES_VAR=COVERAGE_C_SOURCES

# Generate auxiliary file with the list of sources without unit tests (printed for information)
$(UNCOVERED_SRC_LIST_FILE): $(COVERAGE_TARGETS)
	@echo "\nGenerating uncovered files list..."
#	Usage: coverage_helper.sh --find-uncovered-src COVR_DATA_FILE OUT_UNCOVR_SRC_FILE SRC_LIST
	./system/coverage_helper.sh --find-uncovered-src $(COVERAGE_DATA_LIST_FILE) $@ "$(COVERAGE_C_SOURCES)"
	@echo "\nUncovered files found:"; cat $@
	@echo "\nUncovered files list saved: $@"

endif #ifneq ($(COVERAGE_NO_ADD_UNCOVERED_FILES),1)


# Project-only coverage: add the firmware code lines
ifneq ($(COVERAGE_NO_ADD_UNCOVERED_FILES),1)

# Add the firmware code lines of every model to dependencies for "coverage-project" target
PROJECT_COVERAGE_DEPS += $(PROJECT_UNCOVERED_SRC_LIST_FILE) $(PROJECT_COVERAGE_MODELS_JSON)
PROJECT_COVERAGE_ADD_TRACE_FILES += -a $(PROJECT_COVERAGE_MODELS_JSON)

# Generate a data file with the code lines of all model firmwares (project-only)
$(PROJECT_COVERAGE_MODELS_JSON): $(PROJECT_COVERAGE_MODEL_DIRS) $(PROJECT_COVERAGE_TARGETS)
	@gcovr -r . --gcov-executable $(COVERAGE_GCOV_BIN) $(COVERAGE_FUNC_MERGE_MODE) --json -o $@ $(PROJECT_COVERAGE_REPORT_DIR)/models
	@echo "Firmware code lines of $(words $(MODEL_LIST)) models saved: $@"

# Compile the sources for the notes of one model (project-only)
$(PROJECT_COVERAGE_MODEL_DIRS): $(PROJECT_COVERAGE_REPORT_DIR)/models/%: prepare_project_coverage_dir
	@MODEL_DEFINE=$* "$(MAKE)" --no-print-directory coverage_model_notes \
		COVERAGE_MODEL_DIR=$@ COVERAGE_MODEL_SOURCES_VAR=PROJECT_C_SOURCES

# Generate auxiliary file with the list of sources without unit tests (project-only, printed for information)
$(PROJECT_UNCOVERED_SRC_LIST_FILE): $(PROJECT_COVERAGE_DATA_LIST_FILE) $(PROJECT_COVERAGE_TARGETS)
	@echo "\nGenerating uncovered files list (project-only)..."
#	Usage: coverage_helper.sh --find-uncovered-src COVR_DATA_FILE OUT_UNCOVR_SRC_FILE SRC_LIST
	./system/coverage_helper.sh --find-uncovered-src $(PROJECT_COVERAGE_DATA_LIST_FILE) $@ "$(PROJECT_C_SOURCES)"
	@echo "\nUncovered files found:"; cat $@
	@echo "\nUncovered files list saved: $@"

endif #ifneq ($(COVERAGE_NO_ADD_UNCOVERED_FILES),1)


#######################################
# Code lines of a model firmware
#######################################

# Code lines are those the compiler writes into the .gcno notes. The unit tests give notes only for the files they
# build, with their own defines, so every source is also compiled for every model in MODEL_LIST with its defines and
# includes, as in the firmware, by the unit tests' compiler (-S: nothing is assembled, linked or run). gcovr adds the
# code lines of these notes with zero hits; code no model builds doesn't get into the report.
# A sub-make per model (MODEL_DEFINE) gets the model's DEFS, C_INCLUDES and sources from the project Makefile, so the
# source list is passed by the variable name. The notes are named by source base names, as the firmware objects.
# One gcovr reads the notes of all models, after the unit tests' gcovr (it falls back to the project root when gcov
# fails in the test directory): gcov writes its .gcov files into the root, and parallel runs over the same sources
# overwrite each other's files.
# "separate" function merge mode: a submodule function is often defined per model in different #if branches.
# -fsigned-char and -fshort-enums give the notes the char and enum types of the firmware (arm-none-eabi makes enums
# short by default): its size checks (static_assert, wbm_asserts.h) fail with the host ones.
# A source that doesn't compile without a file only the firmware build generates needs COVERAGE_NOTES_FLAGS in the
# project Makefile, the target fails otherwise: a source left out would drop from the report and raise the percentage.
# macOS: the Apple compilers reject the ELF section names of the firmware, so clang builds the notes for the model's ARM
# CPU, with the newlib headers of the firmware toolchain.
ifneq ($(COVERAGE_MODEL_DIR),)
COVERAGE_MODEL_SOURCES = $($(COVERAGE_MODEL_SOURCES_VAR))
COVERAGE_MODEL_NOTES = $(addprefix $(COVERAGE_MODEL_DIR)/,$(notdir $(COVERAGE_MODEL_SOURCES:.c=.s)))
ifeq ($(UNAME_S),Darwin)
COVERAGE_NOTES_CC := clang --target=arm-none-eabi $(MCU) -isystem $(shell $(CC) -print-sysroot)/include
else
COVERAGE_NOTES_CC := $(COVERAGE_GCC_BIN)
endif

.PHONY: coverage_model_notes
coverage_model_notes: $(COVERAGE_MODEL_NOTES)

# The directory is made by the recipe: a rule for it would clash with the top-level rule of the same path.
# No log_print header among the prerequisites, unlike the firmware objects: only LOG_PRINT_ENABLED includes it
$(COVERAGE_MODEL_DIR)/%.s: %.c
	@mkdir -p $(@D)
	@$(COVERAGE_NOTES_CC) $(COVERAGE_NOTES_FLAGS) $(addprefix -D, $(DEFS)) $(addprefix -I, $(C_INCLUDES)) -fsigned-char \
		-fshort-enums -ffreestanding -std=gnu11 -O0 -w -ftest-coverage -DMODULE_NAME=$(subst -,_,$(basename $(notdir $<))) \
		-S $< -o $@ || { echo "Can't compile $< for the code lines of $(notdir $(COVERAGE_MODEL_DIR)) in the coverage" \
		"report. If it needs a file only the firmware build generates, give a stand-in with COVERAGE_NOTES_FLAGS." >&2; exit 1; }
endif


# Generate summary coverage report for project (main target)
# List of dependencies COVERAGE_DEPS is assigned above
coverage: $(COVERAGE_DEPS)
	@echo "\n\n================= Generating summary coverage report for project =================\n"

#	Print extra debug information about COVERAGE_EXTRA_FLAGS, COVERAGE_NO_ADD_UNCOVERED_FILES and COVERAGE_FAIL_UNDER variables
	@echo "COVERAGE_EXTRA_FLAGS = $(COVERAGE_EXTRA_FLAGS)"
	@echo "COVERAGE_NO_ADD_UNCOVERED_FILES = $(COVERAGE_NO_ADD_UNCOVERED_FILES)"
	@echo "COVERAGE_FAIL_UNDER = $(COVERAGE_FAIL_UNDER)"
	@if [ -z "$(COVERAGE_NO_ADD_UNCOVERED_FILES)" ] || [ "$(COVERAGE_NO_ADD_UNCOVERED_FILES)" != "1" ]; then \
		echo "Uncovered source files will be added in the report"; \
	else \
		echo "Uncovered source files will NOT be added in the report"; \
	fi

#	Read list of JSON data files from a file, JSON_LIST is used to resolve COVERAGE_ADD_TRACE_FILES value
	$(eval JSON_LIST := $(shell cat $(COVERAGE_DATA_LIST_FILE)))
	@echo "\nCoverage data files found: $(JSON_LIST)\n"

#	Generate .html and .xml coverage reports
	@gcovr $(COVERAGE_ADD_TRACE_FILES) $(COVERAGE_FUNC_MERGE_MODE) $(COVERAGE_FILTERS_STR) --html-details $(COVERAGE_REPORT_FILE) \
		--cobertura $(COVERAGE_XML_REPORT_FILE) --cobertura-pretty
#	Print project coverage report with optional check minimum coverage level
	@gcovr -s $(COVERAGE_ADD_TRACE_FILES) $(COVERAGE_FUNC_MERGE_MODE) $(COVERAGE_FILTERS_STR) $(COVERAGE_EXTRA_FLAGS)
#	Print information about generated reports
	@echo "\nSummary project coverage report saved: file://$(CURDIR)/$(COVERAGE_REPORT_FILE)"
	@echo "Summary project coverage .xml report saved: file://$(CURDIR)/$(COVERAGE_XML_REPORT_FILE)\n"

# Generate coverage data files for each unittest which have "coverage" target
$(COVERAGE_TARGETS): $(COVERAGE_DATA_LIST_FILE)
	$(eval COV_DIR := $(subst COVERAGE_,,$@))
#	Usage: coverage_helper.sh --make-coverage TEST_DIR OUT_COVR_DATA_FILE [GCC_BIN]
#	$(MAKE) makes the line a recursive make: the make of the unittest gets the job slots of this one and builds its tests
#	in parallel under -jN
	@if [ -f "$(COV_DIR)/Makefile" ]; then \
		MAKE="$(MAKE)" ./system/coverage_helper.sh --make-coverage $(COV_DIR) $(COVERAGE_DATA_LIST_FILE) $(GCC_BIN); \
	fi

# Create an empty file in which the list of coverage data files will be written.
# Depends on prepare_coverage_dir (a normal, not order-only, prerequisite) so
# that it is recreated AFTER the report directory is wiped on every run — even
# when the file lingers from a previous run and `make clean` was not called.
$(COVERAGE_DATA_LIST_FILE): prepare_coverage_dir
	@touch $@

# Create directory for coverage report
$(COVERAGE_REPORT_DIR):
	mkdir -p $@

# Prepare a clean coverage report directory.
# Used as a normal (not order-only) prerequisite of everything that writes into
# the report directory, so the wipe + recreate is serialized ahead of the
# parallel coverage jobs (instead of racing them as plain sibling
# prerequisites). It must be a NORMAL prereq: this target is phony, so a normal
# dependency forces the dependent files to be regenerated after each wipe; an
# order-only prereq would let a lingering file look up-to-date while the rm -rf
# deletes it, reintroducing the race.
.PHONY: prepare_coverage_dir
prepare_coverage_dir:
	@rm -rf $(COVERAGE_REPORT_DIR)
	@mkdir -p $(COVERAGE_REPORT_DIR)

# Prepare a clean project-only coverage report directory (see prepare_coverage_dir).
.PHONY: prepare_project_coverage_dir
prepare_project_coverage_dir:
	@rm -rf $(PROJECT_COVERAGE_REPORT_DIR)
	@mkdir -p $(PROJECT_COVERAGE_REPORT_DIR)

# Remove coverage report directory
remove_report_dir:
	rm -rf $(COVERAGE_REPORT_DIR)

# Remove project-only coverage report directory
remove_project_report_dir:
	rm -rf $(PROJECT_COVERAGE_REPORT_DIR)

#######################################
# Project-only coverage targets
#######################################

# Generate summary coverage report for project only (excluding submodules)
# List of dependencies PROJECT_COVERAGE_DEPS is assigned above
coverage-project: $(PROJECT_COVERAGE_DEPS)
	@echo "\n\n================= Generating summary coverage report for project (excluding submodules) =================\n"

#	Print extra debug information
	@echo "COVERAGE_EXTRA_FLAGS = $(COVERAGE_EXTRA_FLAGS)"
	@echo "COVERAGE_NO_ADD_UNCOVERED_FILES = $(COVERAGE_NO_ADD_UNCOVERED_FILES)"
	@echo "COVERAGE_FAIL_UNDER = $(COVERAGE_FAIL_UNDER)"
	@if [ -z "$(COVERAGE_NO_ADD_UNCOVERED_FILES)" ] || [ "$(COVERAGE_NO_ADD_UNCOVERED_FILES)" != "1" ]; then \
		echo "Uncovered source files will be added in the report"; \
	else \
		echo "Uncovered source files will NOT be added in the report"; \
	fi

#	Read list of JSON data files from a file
	$(eval PROJECT_JSON_LIST := $(shell cat $(PROJECT_COVERAGE_DATA_LIST_FILE)))
	@echo "\nCoverage data files found: $(PROJECT_JSON_LIST)\n"

#	Generate .html and .xml coverage reports
	@gcovr $(PROJECT_COVERAGE_ADD_TRACE_FILES) $(COVERAGE_FUNC_MERGE_MODE) $(PROJECT_COVERAGE_FILTERS_STR) --html-details $(PROJECT_COVERAGE_REPORT_FILE) \
		--cobertura $(PROJECT_COVERAGE_XML_REPORT_FILE) --cobertura-pretty
#	Print project coverage report with optional check minimum coverage level
	@gcovr -s $(PROJECT_COVERAGE_ADD_TRACE_FILES) $(COVERAGE_FUNC_MERGE_MODE) $(PROJECT_COVERAGE_FILTERS_STR) $(COVERAGE_EXTRA_FLAGS)
#	Print information about generated reports
	@echo "\nSummary project coverage report saved: file://$(CURDIR)/$(PROJECT_COVERAGE_REPORT_FILE)"
	@echo "Summary project coverage .xml report saved: file://$(CURDIR)/$(PROJECT_COVERAGE_XML_REPORT_FILE)\n"

# Generate coverage data files for each project unittest which have "coverage" target
$(PROJECT_COVERAGE_TARGETS): $(PROJECT_COVERAGE_DATA_LIST_FILE)
	$(eval COV_DIR := $(subst PROJECT_COVERAGE_,,$@))
#	Usage: coverage_helper.sh --make-coverage TEST_DIR OUT_COVR_DATA_FILE [GCC_BIN]
#	$(MAKE): a recursive make, see $(COVERAGE_TARGETS)
	@if [ -f "$(COV_DIR)/Makefile" ]; then \
		MAKE="$(MAKE)" ./system/coverage_helper.sh --make-coverage $(COV_DIR) $(PROJECT_COVERAGE_DATA_LIST_FILE) $(GCC_BIN); \
	fi

# Create an empty file for project coverage data list
$(PROJECT_COVERAGE_DATA_LIST_FILE): prepare_project_coverage_dir
	@touch $@

# Create directory for project coverage report
$(PROJECT_COVERAGE_REPORT_DIR):
	mkdir -p $@
