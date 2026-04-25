// =============================================================================
// NoisePlethoraPlugin.hpp  (NT port)
// =============================================================================
// Replaces the original STL-/VCV-based base class with a tiny runtime-free
// version suitable for Disting NT (-fno-rtti -fno-exceptions, no heap from
// step()).
//
// Key differences vs upstream:
//   - No std::map / std::function / std::shared_ptr / std::string factory.
//   - REGISTER_PLUGIN() is a no-op; algorithm registration is done explicitly
//     in PluginRegistry.hpp via a constexpr table.
//   - Same sample-pull processGraph() pattern: each call returns one float in
//     [-1, +1], refilling the 128-sample int16 ring buffer on demand.
//
// Vendored P_*.hpp files compile against this header unchanged.
// =============================================================================
#pragma once

#include "../teensy/TeensyAudioReplacements.hpp"
#include "../teensy/dspinst.h"     // int16_to_float_1v

class NoisePlethoraPlugin {
public:
    NoisePlethoraPlugin() {}
    virtual ~NoisePlethoraPlugin() {}

    NoisePlethoraPlugin(const NoisePlethoraPlugin&) = delete;
    NoisePlethoraPlugin& operator=(const NoisePlethoraPlugin&) = delete;

    // Lifecycle hooks (overridden by P_* algorithms).
    virtual void init() {}
    virtual void process(float k1, float k2) { (void)k1; (void)k2; }

    // Sample-pull: returns one float in [-1, +1]. Refills the internal 128-sample
    // int16 buffer from processGraphAsBlock() on demand. Maps cleanly onto
    // Disting NT's per-frame step() loop.
    float processGraph() {
        if (blockBuffer.empty()) {
            processGraphAsBlock(blockBuffer);
        }
        return int16_to_float_1v(blockBuffer.shift());
    }

    // Optional: VCV used these to wire stream graphs. The NT port doesn't
    // call them, but we keep them pure-virtual so vendored P_*.hpp algorithms
    // (which all `override` both) compile without edits.
    virtual AudioStream& getStream() = 0;
    virtual unsigned char getPort() = 0;

protected:
    // Subclass fills the supplied buffer with AUDIO_BLOCK_SAMPLES int16 samples.
    virtual void processGraphAsBlock(TeensyBuffer& blockBuffer) = 0;

    TeensyBuffer blockBuffer;
};

// -----------------------------------------------------------------------------
// REGISTER_PLUGIN: original used a static Registrar<T> to wire the class into
// MyFactory. NT port uses an explicit constexpr table (see PluginRegistry.hpp),
// so this macro is a no-op. Vendored P_*.hpp files compile unchanged.
// -----------------------------------------------------------------------------
#define REGISTER_PLUGIN(NAME) /* registration is handled in PluginRegistry.hpp */
