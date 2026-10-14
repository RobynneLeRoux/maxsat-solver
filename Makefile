# GENAI: Gemini was used to convert a meson.build file into a Makefile with the same settings/functionality.
BUILD_DIR  ?= build/debug
RELEASE_DIR ?= build/release
PROFILE_DIR ?= build/profile
COV_DIR ?= build/cov

CXX = clang++

# Default optimization flags (can be overridden by `make release`)
OPT_FLAGS ?= -g -O0
SAN_FLAGS ?= -fsanitize=address,undefined -fno-omit-frame-pointer

# Base flags
CXXFLAGS = -std=c++23 -stdlib=libc++ -Iinclude -MMD -MP -fno-sanitize-trap=undefined -Wall -Wextra $(OPT_FLAGS) $(SAN_FLAGS)
LDFLAGS  = -fno-sanitize-trap=undefined $(OPT_FLAGS) $(SAN_FLAGS)

# ==========================================
# Target Binary Paths
# ==========================================
LIB_CORE = $(BUILD_DIR)/src/libmaxsat_core.a
APP_MAIN = $(BUILD_DIR)/app/main

# ==========================================
# Sources and Objects
# ==========================================
# Core Library
LIB_SRCS = \
	src/AlgoM.cpp \
	src/AlgoM02.cpp \
	src/AlgoM03.cpp \
	src/Formula.cpp \
	src/Parser.cpp \
	src/Preprocessor.cpp

# Main App
MAIN_SRC = app/main.cpp

# Catch2 Amalgamated Dependency
CATCH2_SRC = tests/catch2/catch_amalgamated.cpp

# Tests: Automatically finds all test .cpp files directly under tests/
TEST_SRCS = $(wildcard tests/*.cpp)

# Object Files
LIB_OBJS   = $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(LIB_SRCS))
MAIN_OBJ   = $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(MAIN_SRC))
CATCH2_OBJ = $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(CATCH2_SRC))
TEST_OBJS  = $(patsubst tests/%.cpp, $(BUILD_DIR)/tests/%.o, $(TEST_SRCS))

# Consolidate all object files
ALL_OBJS   = $(LIB_OBJS) $(MAIN_OBJ) $(CATCH2_OBJ) $(TEST_OBJS)

# Map every object file to its corresponding dependency (.d) file
DEPS       = $(ALL_OBJS:.o=.d)

# Map each test source to its target executable (e.g., tests/FooTest.cpp -> $(BUILD_DIR)/tests/FooTest)
TEST_BINS  = $(patsubst tests/%.cpp, $(BUILD_DIR)/tests/%, $(TEST_SRCS))

# Coverage binary references for llvm-cov
COV_TEST_BINS = $(patsubst tests/%.cpp, $(COV_DIR)/tests/%, $(TEST_SRCS))
COV_FIRST     = $(firstword $(COV_TEST_BINS))
COV_REST      = $(addprefix -object=, $(filter-out $(COV_FIRST), $(COV_TEST_BINS)))

# ==========================================
# Build Rules
# ==========================================
.PHONY: all release test profile yalsat coverage coverage-html clean

# Default build target
all: $(APP_MAIN) $(TEST_BINS)

# Release target invoking sub-make with defined RELEASE_DIR
release:
	$(MAKE) BUILD_DIR=$(RELEASE_DIR) OPT_FLAGS="-O3 -march=native -flto -DNDEBUG" SAN_FLAGS=""

profile:
	$(MAKE) BUILD_DIR=$(PROFILE_DIR) OPT_FLAGS="-O0 -march=native -DNDEBUG -g -fno-omit-frame-pointer -fno-optimize-sibling-calls" SAN_FLAGS=""

coverage:
	@if [ -z "$(TEST_SRCS)" ]; then \
		echo "Error: No test .cpp files found in tests/"; exit 1; \
	fi
	@mkdir -p $(COV_DIR)
	@rm -f $(COV_DIR)/*.profraw $(COV_DIR)/coverage.profdata
	$(MAKE) BUILD_DIR=$(COV_DIR) OPT_FLAGS="-g -O0 -fprofile-instr-generate -fcoverage-mapping -DNDEBUG" SAN_FLAGS=""
	@echo "Running tests to collect profile data..."
	@for test_bin in $(COV_TEST_BINS); do \
		echo "Running $$test_bin..."; \
		LLVM_PROFILE_FILE="$(COV_DIR)/cov-%p.profraw" ./$$test_bin || exit 1; \
	done
	@echo "Merging profile data..."
	xcrun llvm-profdata merge -sparse $(COV_DIR)/*.profraw -o $(COV_DIR)/coverage.profdata
	@echo "=== Full Project Coverage Report ==="
	xcrun llvm-cov report $(COV_FIRST) $(COV_REST) -instr-profile=$(COV_DIR)/coverage.profdata src include

# Generate interactive HTML coverage report
coverage-html: coverage
	@echo "Generating HTML coverage report..."
	@rm -rf $(COV_DIR)/coverage_html
	xcrun llvm-cov show $(COV_FIRST) $(COV_REST) \
		-instr-profile=$(COV_DIR)/coverage.profdata \
		src include \
		-format=html \
		-output-dir=$(COV_DIR)/coverage_html
	@echo "HTML coverage report available at: $(COV_DIR)/coverage_html/index.html"
# Generic rule for compiling .cpp files to .o object files
$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 1. Static Core Library
$(LIB_CORE): $(LIB_OBJS)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

# 2. Main Executable -> builddir/app/main
$(APP_MAIN): $(MAIN_OBJ) $(LIB_CORE)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# 3. Test Executables -> builddir/tests/<TestName>
$(BUILD_DIR)/tests/%: $(BUILD_DIR)/tests/%.o $(CATCH2_OBJ) $(LIB_CORE)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -O3 -g -UNDEBUG -o $@ $^ $(LDFLAGS)

# Run all test binaries sequentially
test: $(TEST_BINS)
	@for test_bin in $(TEST_BINS); do \
		echo "Running $$test_bin..."; \
		./$$test_bin || exit 1; \
	done

# ==========================================
# yalsat (https://github.com/arminbiere/yalsat.git)
# ==========================================
YALSAT_DIR = build/yalsat
YALSAT_BIN = $(YALSAT_DIR)/yalsat

yalsat: $(YALSAT_BIN)

$(YALSAT_BIN):
	@if [ ! -d "$(YALSAT_DIR)" ]; then \
		echo "Cloning yalsat repo..."; \
		git clone https://github.com/arminbiere/yalsat.git $(YALSAT_DIR); \
	fi
	@echo "Building yalsat..."
	cd $(YALSAT_DIR) && ./configure.sh && $(MAKE)

# Clean all build outputs
clean:
	rm -rf $(BUILD_DIR) $(RELEASE_DIR) $(PROFILE_DIR) $(COV_DIR)

# Include generated dependency files (- suppresses missing file errors on fresh builds)
-include $(DEPS)
