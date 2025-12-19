# ==============================================================================
# Global Configuration
# ==============================================================================

# Path to the API headers for both targets
ifndef NT_API_PATH
    NT_API_PATH := ../distingNT_API/include
endif
INCLUDE_PATH := $(NT_API_PATH)
HEADERS_PATH := ./include

# ==============================================================================
# Embedded Target (Cross-Compilation using ARM toolchain)
# ==============================================================================

TARGET_TOOLCHAIN := arm-none-eabi-c++
TARGET_CPPFLAGS := -std=c++11 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -fno-rtti -fno-exceptions -Os -fPIC -Wall

# Sources and Outputs for the embedded plugins
EMBEDDED_SRCS := $(filter-out UnitTests.cpp, $(wildcard *cpp))
EMBEDDED_OBJS := $(patsubst %.cpp,plugins/%.o,$(EMBEDDED_SRCS))

# Default target: builds the embedded object files
all: $(EMBEDDED_OBJS)

plugins/%.o: %.cpp
	@mkdir -p $(@D)
	$(TARGET_TOOLCHAIN) $(TARGET_CPPFLAGS) -I$(INCLUDE_PATH) -I$(HEADERS_PATH) -c -o $@ $^

# ==============================================================================
# Host Target (WSL/Linux Compilation for Unit Tests)
# ==============================================================================

CXX_WSL := g++
WSL_BUILD_DIR := bin
TEST_SRC := UnitTests.cpp
TEST_EXE := $(WSL_BUILD_DIR)/UnitTests

# Target to compile and link the UnitTests executable
.PHONY: test
test: $(TEST_EXE)
	@echo "Unit tests compiled. Run with: ./$(TEST_EXE)"

$(TEST_EXE): $(TEST_SRC)
	@mkdir -p $(WSL_BUILD_DIR)
	@echo "Compiling and linking host executable for tests..."
	$(CXX_WSL) -std=c++11 -Wall -g -I$(INCLUDE_PATH) -I$(HEADERS_PATH) -o $@ $^

# ==============================================================================
# Utility Targets
# ==============================================================================

clean:
	@echo "Cleaning embedded object files..."
	rm -f $(EMBEDDED_OBJS)
	@echo "Cleaning host test executable..."
	rm -rf $(WSL_BUILD_DIR)