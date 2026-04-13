# ==============================================================================
# Global Configuration
# ==============================================================================

# Path to the API headers for both targets
ifndef NT_API_PATH
    NT_API_PATH := ../distingNT_API/include
endif
INCLUDE_PATH := $(NT_API_PATH)
HEADERS_PATH := ../LofiParts

# ==============================================================================
# Embedded Target (Cross-Compilation using ARM toolchain)
# ==============================================================================

TARGET_TOOLCHAIN := arm-none-eabi-c++
TARGET_CPPFLAGS := -std=c++17 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -fno-rtti -fno-exceptions -Os -fPIC -Wall

# Sources and Outputs for the embedded plugin (single relocatable .o)
EMBEDDED_SRCS := PolyLofi.cpp PolyLofiVoice.cpp
EMBEDDED_OBJS := $(patsubst %.cpp,plugins/%.tmp.o,$(EMBEDDED_SRCS))
PLUGIN_OBJ    := plugins/PolyLofi.o

# Default target: builds the single combined plugin object
all: $(PLUGIN_OBJ)

# Link individual objects into one relocatable .o
$(PLUGIN_OBJ): $(EMBEDDED_OBJS)
	arm-none-eabi-ld -r -o $@ $^

plugins/%.tmp.o: %.cpp
	@mkdir -p $(@D)
	$(TARGET_TOOLCHAIN) $(TARGET_CPPFLAGS) -I$(INCLUDE_PATH) -I$(HEADERS_PATH) -c -o $@ $^

# ==============================================================================
# Utility Targets
# ==============================================================================

clean:
	@echo "Cleaning embedded object files..."
	rm -f $(EMBEDDED_OBJS) $(PLUGIN_OBJ)
	@echo "Cleaning host test executable..."
	rm -rf $(TEST_BUILD_DIR)

# ==============================================================================
# Integration Tests (shared harness)
# ==============================================================================

PLUGIN_SRCS   := PolyLofi.cpp PolyLofiVoice.cpp
TEST_SRCS     := tests/test_integration.cpp
EXTRA_INCLUDE := -I../LofiParts
include ../test_harness/test_harness.mk