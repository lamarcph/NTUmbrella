# NTUmbrella Test Harness

Shared, reusable headless test infrastructure for all disting NT plugins in this monorepo.

## Contents

| File | Purpose |
|------|---------|
| `nt_api_stub.cpp` | Provides all `extern "C"` symbols from `distingnt/api.h` as stubs. Plugins link against this instead of the real firmware. |
| `test_framework.h` | Lightweight test framework: `TestResult`, `ASSERT_*` macros, `TestRunner::run()`. Compatible with existing LofiOsc test style. |
| `plugin_harness.h` | `PluginInstance` class — manages the full plugin lifecycle: `pluginEntry` → `calculateRequirements` → `construct` → `parameterChanged` → `step` → `midiMessage`. Heap-allocates SRAM/DRAM/DTC/ITC pools. |
| `wav_writer.h` | Single-header 16-bit PCM WAV writer for capturing test audio output. |
| `test_harness.mk` | Shared Makefile include — provides `test`, `test-run`, and `test-clean` targets. |

## Quick Start

### 1. Create a test file in your plugin directory

```cpp
// MyPlugin/tests/test_integration.cpp
#include "test_framework.h"
#include "plugin_harness.h"
#include "wav_writer.h"

TestResult test_plugin_loads() {
    TEST_BEGIN("Plugin loads and constructs");
    PluginInstance plugin;
    ASSERT_TRUE(plugin.load(), "pluginEntry returned factory");
    ASSERT_TRUE(plugin.construct(), "construct succeeded");
    TEST_PASS();
}

TestResult test_produces_audio() {
    TEST_BEGIN("Note-on produces audio");
    PluginInstance plugin;
    plugin.load();
    plugin.construct();
    plugin.midiNoteOn(0, 60, 100);
    int frames = 128;
    plugin.step(frames);
    // Output bus is plugin-specific — check your kParamOutput default.
    float* bus = plugin.getBus(12, frames);  // bus 13, 0-indexed = 12
    ASSERT_FALSE(PluginInstance::isSilent(bus, frames), "output is not silent");
    TEST_PASS();
}

int main() {
    return TestRunner::run({ test_plugin_loads, test_produces_audio });
}
```

### 2. Add to your Makefile

```makefile
# At the end of your existing Makefile:
PLUGIN_SRCS   := MyPlugin.cpp
TEST_SRCS     := tests/test_integration.cpp
EXTRA_INCLUDE := -I../LofiParts   # if needed
include ../test_harness/test_harness.mk
```

### 3. Build and run

```bash
cd MyPlugin
make test        # compile
make test-run    # compile + run
```

## What Gets Stubbed

The `nt_api_stub.cpp` provides:

- **`NT_globals`** — configurable sample rate (default 96 kHz), max frames (128), work buffer
- **Drawing** — all no-ops (`NT_drawText`, `NT_drawShapeI/F`, `NT_screen`)
- **MIDI send** — all no-ops (`NT_sendMidiByte`, `NT_sendMidi2ByteMessage`, etc.)
- **Parameters** — `NT_setParameterRange` with real scaling logic; other param functions are no-ops
- **String formatting** — `NT_intToString`, `NT_floatToString` using sprintf
- **Slots** — `NT_algorithmIndex`, `NT_algorithmCount`, `NT_getSlot` stubs
- **Misc** — `NT_getCpuCycleCount`, `NT_log` (prints to stderr), `NT_random`

## Configuring Sample Rate

```cpp
NtTestHarness::setSampleRate(48000);  // before plugin.construct()
NtTestHarness::setMaxFrames(64);
```

## WAV Output

```cpp
WavWriter wav("test_output.wav", 96000, 1);  // mono
for (int block = 0; block < 100; ++block) {
    float* bus = plugin.step(128);
    wav.writeMono(bus + 12 * 128, 128);  // bus 13
}
wav.close();
```
