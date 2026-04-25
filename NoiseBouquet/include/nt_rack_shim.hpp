// =============================================================================
// nt_rack_shim.hpp
// =============================================================================
// Replaces VCV Rack's <rack.hpp> for the NoiseBouquet port. Provides only
// the symbols actually used by the vendored teensy audio shim (`teensy/`)
// and Noise Plethora plugin sources (`algos/`):
//   - rack::dsp::RingBuffer<T,N>           (used as TeensyBuffer)
//   - APP->engine->getSampleRate() / getSampleTime()  (queried by every
//     synth_*/effect_*/filter_* unit to scale frequencies and times)
//
// Stateless: the "engine" reads NT_globals.sampleRate directly each call,
// so there are no plugin-local writable globals (which the NT loader
// would not relocate). See ../.github/skills/distingnt-plugin-loader.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>   // std::min / std::max used by teensy units

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795f
#endif

namespace rack {
namespace dsp {

// Fixed-size single-producer/single-consumer ring buffer.
// Matches the subset of rack::dsp::RingBuffer used by NoisePlethoraPlugin:
//   - empty()
//   - shift()              -> pop one element
//   - pushBuffer(T*, n)    -> push n elements in bulk
template <typename T, size_t N>
class RingBuffer {
public:
    RingBuffer() : start(0), end(0) {}

    bool empty() const { return start == end; }
    bool full()  const { return (end - start) == (int)N; }
    size_t size() const { return end - start; }

    void push(T t) {
        data[end & (N - 1)] = t;
        ++end;
    }

    T shift() {
        T v = data[start & (N - 1)];
        ++start;
        return v;
    }

    void pushBuffer(const T* src, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            data[end & (N - 1)] = src[i];
            ++end;
        }
    }

private:
    static_assert((N & (N - 1)) == 0, "RingBuffer N must be a power of two");
    T   data[N];
    int start;
    int end;
};

// Minimal stand-in for rack::dsp::Timer used by BasuraTotal.
struct Timer {
    float time = 0.0f;
    void reset() { time = 0.0f; }
    void process(float dt) { time += dt; }
};

} // namespace dsp
} // namespace rack

// Some vendored Befaco algorithms refer to `dsp::Timer` directly.
namespace dsp {
using Timer = rack::dsp::Timer;
}

// Minimal deterministic RNG shim for algorithms that need a stable uniform RNG.
namespace nt_random {
inline float uniform() {
    static uint32_t state = 0x12345678u;
    state = state * 1664525u + 1013904223u;
    return (float)((state >> 8) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}
}

// -----------------------------------------------------------------------------
// APP->engine->getSampleRate() / getSampleTime() shim
// -----------------------------------------------------------------------------
// VCV exposes a global APP pointer of type rack::Context*. The teensy units
// only ever call APP->engine->getSampleRate()/getSampleTime(), so we provide
// a minimal facade.
//
// IMPORTANT (Disting NT): the plugin loader does not apply runtime
// relocations to a plugin's `.data.rel` section, and does not run the
// plugin's `.init_array` global ctors either. That makes any plugin-local
// pointer-to-global (e.g. `Context* APP = &g_context;`) unsafe — it stays
// at 0 at runtime and the first dereference hangs the unit.
//
// Workaround: the shim carries no state. Engine/Context are empty structs
// whose getter methods read directly from `NT_globals` (a const symbol the
// host firmware exports — the loader resolves it as an external symbol so
// no plugin-local data relocation is required). `APP` is a macro that
// returns a pointer into the plugin's own `.rodata`, no relocation needed.
// -----------------------------------------------------------------------------
#include <distingnt/api.h>     // NT_globals

namespace nt_shim {

// The vendored Befaco/Teensy units assume a fixed 44.1 kHz sample rate
// (AUDIO_SAMPLE_RATE_EXACT). We honour that by always reporting 44100 here;
// NoiseBouquet.cpp's step() resamples the algo output to NT's actual rate.
// This keeps pitches and times identical to the original hardware.
struct Engine {
    float getSampleRate() const { return 44100.0f; }
    float getSampleTime() const { return 1.0f / 44100.0f; }
    void  setSampleRate(float /*sr*/) {}
};

struct Context {
    Engine* engine;   // patched at first call by nt_app_ptr()
};

// Function-local trivial statics live in .bss (zero-initialised). Because
// the types are trivially-constructible, GCC does NOT emit a guard variable
// or `.init_array` entry. The pointer fixup is a runtime store inside the
// function body, where the relocation against `nt_engine_ptr` lives in
// `.text` -- and those relocations are applied correctly by the NT loader.
inline Engine* nt_engine_ptr() {
    static Engine e;
    return &e;
}
inline Context* nt_app_ptr() {
    static Context c;            // c.engine starts as nullptr (.bss)
    c.engine = nt_engine_ptr();  // re-stored every call; cheap, no .data.rel
    return &c;
}

// Pseudo-global: every read of `APP` calls `nt_app_ptr()`. Operator
// precedence makes `APP->engine->getSampleRate()` parse exactly as before.
// Use a macro (rather than a function-pointer global) to avoid generating
// a `.data.rel` entry for the pointer's storage.
#define APP (::nt_shim::nt_app_ptr())

// Kept for source compatibility; the shim has no state to set.
inline void setSampleRate(float /*sr*/) {}

} // namespace nt_shim

// VCV Rack's logging macro. Some vendored algorithms (e.g. P_TeensyAlt) call
// DEBUG() from init(); on the NT we drop the message on the floor.
#ifndef DEBUG
#define DEBUG(...) ((void)0)
#endif
