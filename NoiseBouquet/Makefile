# =============================================================================
# NoiseBouquet/Makefile
# =============================================================================
# Builds plugins/NoiseBouquet.o from NoiseBouquet.cpp + nt_rack_shim.cpp +
# the vendored teensy/* sources. Combined into a single relocatable object
# via `arm-none-eabi-ld -r`, matching the Disting NT plugin layout.
# =============================================================================

ifndef NT_API_PATH
    NT_API_PATH := ../distingNT_API/include
endif

INCLUDE_FLAGS := -I$(NT_API_PATH) -Iinclude -I.

TARGET_TOOLCHAIN := arm-none-eabi-c++
TARGET_CPPFLAGS  := -std=c++17 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard \
                    -mthumb -fno-rtti -fno-exceptions -Os -fPIC -Wall

EMBEDDED_SRCS := NoiseBouquet.cpp \
                 nt_rack_shim.cpp \
                 teensy/effect_flange.cpp \
                 teensy/effect_granular.cpp \
                 teensy/synth_pwm.cpp \
                 teensy/synth_pinknoise.cpp \
                 teensy/synth_whitenoise.cpp \
                 teensy/filter_variable.cpp \
                 teensy/effect_bitcrusher.cpp \
                 teensy/effect_combine.cpp \
                 teensy/effect_freeverb.cpp \
                 teensy/effect_multiply.cpp \
                 teensy/effect_wavefolder.cpp

EMBEDDED_OBJS := $(patsubst %.cpp,build/%.tmp.o,$(EMBEDDED_SRCS))
PLUGIN_OBJ    := plugins/NoiseBouquet.o

all: $(PLUGIN_OBJ)

$(PLUGIN_OBJ): $(EMBEDDED_OBJS)
	@mkdir -p $(@D)
	arm-none-eabi-ld -r -o $@ $^

build/%.tmp.o: %.cpp
	@mkdir -p $(@D)
	$(TARGET_TOOLCHAIN) $(TARGET_CPPFLAGS) $(INCLUDE_FLAGS) -c -o $@ $<

clean:
	@echo "Cleaning..."
	rm -rf build
	rm -f $(PLUGIN_OBJ)

.PHONY: all clean

# =============================================================================
# Integration tests (shared host harness)
# =============================================================================
# Brings in `make test`, `make test-run`, `make test-clean` from the shared
# harness. Builds bin/tests with g++, runs offline against nt_api_stub.cpp.
# =============================================================================

PLUGIN_SRCS    := NoiseBouquet.cpp \
                  nt_rack_shim.cpp \
                  teensy/effect_flange.cpp \
                  teensy/effect_granular.cpp \
                  teensy/synth_pwm.cpp \
                  teensy/synth_pinknoise.cpp \
                  teensy/synth_whitenoise.cpp \
                  teensy/filter_variable.cpp \
                  teensy/effect_bitcrusher.cpp \
                  teensy/effect_combine.cpp \
                  teensy/effect_freeverb.cpp \
                  teensy/effect_multiply.cpp \
                  teensy/effect_wavefolder.cpp
TEST_SRCS      := tests/test_integration.cpp \
                  tests/test_effect_units.cpp
EXTRA_INCLUDE  := -Iinclude -I.

include ../test_harness/test_harness.mk
