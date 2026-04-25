# ==============================================================================
# test_harness.mk — Shared Makefile include for headless integration testing
# ==============================================================================
#
# Include this from any plugin's Makefile.  Before including, set:
#
#   PLUGIN_SRCS   = list of plugin .cpp files to compile
#   TEST_SRCS     = list of test .cpp files
#   EXTRA_INCLUDE = any plugin-specific -I flags  (optional)
#   TEST_STD      = C++ standard (defaults to c++17)
#
# Example (PolyLofi/Makefile):
#
#   PLUGIN_SRCS := PolyLofi.cpp
#   TEST_SRCS   := tests/test_integration.cpp
#   EXTRA_INCLUDE := -I../LofiParts
#   include ../test_harness/test_harness.mk
#
# This provides the "test" target which compiles and optionally runs the tests.
# ==============================================================================

# Paths relative to the including Makefile
HARNESS_DIR  ?= ../test_harness
NT_API_PATH  ?= ../distingNT_API/include

# Compiler
TEST_CXX     ?= g++
TEST_STD     ?= c++17
TEST_CXXFLAGS = -std=$(TEST_STD) -Wall -Wextra -Wno-unused-parameter -g -O1 \
                -msse2 -mfpmath=sse \
                -I$(NT_API_PATH) -I$(HARNESS_DIR) $(EXTRA_INCLUDE)

# Build output
TEST_BUILD_DIR ?= bin
TEST_EXE       ?= $(TEST_BUILD_DIR)/tests

# Harness sources (always included)
HARNESS_SRCS = $(HARNESS_DIR)/nt_api_stub.cpp

# All sources to compile
ALL_TEST_SRCS = $(HARNESS_SRCS) $(PLUGIN_SRCS) $(TEST_SRCS)

# ==============================================================================
# Targets
# ==============================================================================

.PHONY: test test-run test-clean

test: $(TEST_EXE)
	@echo ""
	@echo "Tests compiled.  Run with:  ./$(TEST_EXE)"
	@echo "  or:  make test-run"
	@echo ""

test-run: $(TEST_EXE)
	@./$(TEST_EXE)
	@python3 $(HARNESS_DIR)/pgm_to_png.py $(TEST_BUILD_DIR)

$(TEST_EXE): $(ALL_TEST_SRCS)
	@mkdir -p $(TEST_BUILD_DIR)
	@echo "Compiling test harness..."
	$(TEST_CXX) $(TEST_CXXFLAGS) -o $@ $^

test-clean:
	rm -rf $(TEST_BUILD_DIR)
