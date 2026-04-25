// =============================================================================
// plugin_harness.h — Generic disting NT plugin lifecycle harness
// =============================================================================
// Manages the full plugin lifecycle:  pluginEntry → calculateRequirements →
// construct → parameterChanged → step → midiMessage.
//
// Heap-allocates the memory pools that the real NT firmware would provide
// (SRAM, DRAM, DTC, ITC) and drives the plugin through its callbacks.
//
// Reusable across all plugins in NTUmbrella — just link their .cpp and this
// harness together with nt_api_stub.cpp.
// =============================================================================

#pragma once

#include <distingnt/api.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>
#include <cmath>

// Forward declare pluginEntry (provided by the plugin .cpp being tested)
extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data);

// ---------------------------------------------------------------------------
// Allow tests to configure NT_globals before plugin construction
// ---------------------------------------------------------------------------
namespace NtTestHarness {
    void     setSampleRate(uint32_t sr);
    void     setMaxFrames(uint32_t frames);
    uint32_t getSampleRate();
    uint32_t getMaxFrames();
    void     setSetParameterCallback(void (*cb)(uint32_t algIdx, uint32_t param, int16_t value));
}

// ---------------------------------------------------------------------------
// PluginInstance — owns one algorithm instance and its memory
// ---------------------------------------------------------------------------
class PluginInstance {
public:
    PluginInstance() = default;
    ~PluginInstance() { destroy(); }

    // Non-copyable
    PluginInstance(const PluginInstance&) = delete;
    PluginInstance& operator=(const PluginInstance&) = delete;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// Load factory at index `factoryIndex` from the plugin.
    /// Returns false if pluginEntry doesn't provide it.
    bool load(uint32_t factoryIndex = 0) {
        _factory = reinterpret_cast<const _NT_factory*>(
            pluginEntry(kNT_selector_factoryInfo, factoryIndex)
        );
        return _factory != nullptr;
    }

    /// Run calculateStaticRequirements + initialise (shared data).
    void initStatic() {
        if (!_factory) return;
        if (_factory->calculateStaticRequirements) {
            _factory->calculateStaticRequirements(_staticReq);
            if (_staticReq.dram > 0) {
                _staticDram.resize(_staticReq.dram, 0);
                _staticPtrs.dram = _staticDram.data();
            }
            if (_factory->initialise) {
                _factory->initialise(_staticPtrs, _staticReq);
            }
        }
    }

    /// Run calculateRequirements + construct.
    /// After this, the algorithm is live and parameterChanged can be called.
    bool construct(const int32_t* specifications = nullptr) {
        if (!_factory) return false;

        // If no specifications provided, build defaults from factory specs
        std::vector<int32_t> defaultSpecs;
        if (!specifications && _factory->specifications && _factory->numSpecifications > 0) {
            defaultSpecs.resize(_factory->numSpecifications);
            for (uint32_t i = 0; i < _factory->numSpecifications; ++i)
                defaultSpecs[i] = _factory->specifications[i].def;
            specifications = defaultSpecs.data();
        }

        _factory->calculateRequirements(_req, specifications);

        // Allocate memory pools (aligned to 8 bytes for safety)
        _sram.resize(_req.sram + 8, 0);
        _dram.resize(_req.dram + 8, 0);
        _dtc.resize(_req.dtc + 8, 0);
        _itc.resize(_req.itc + 8, 0);

        _ptrs.sram = align8(_sram.data());
        _ptrs.dram = align8(_dram.data());
        _ptrs.dtc  = align8(_dtc.data());
        _ptrs.itc  = align8(_itc.data());

        _algorithm = _factory->construct(_ptrs, _req, specifications);
        if (!_algorithm) return false;

        // Allocate and populate parameter values with defaults
        _paramValues.resize(_req.numParameters, 0);
        if (_algorithm->parameters) {
            for (uint32_t i = 0; i < _req.numParameters; ++i) {
                _paramValues[i] = _algorithm->parameters[i].def;
            }
        }
        _algorithm->v = _paramValues.data();
        _algorithm->vIncludingCommon = _paramValues.data();

        // Fire parameterChanged for every parameter so the plugin
        // can derive cached values from defaults.
        if (_factory->parameterChanged) {
            for (uint32_t i = 0; i < _req.numParameters; ++i) {
                _factory->parameterChanged(_algorithm, (int)i);
            }
        }

        return true;
    }

    void destroy() {
        // Just free our heap allocations — no destructor protocol in NT API
        _algorithm = nullptr;
        _factory = nullptr;
        _sram.clear(); _dram.clear(); _dtc.clear(); _itc.clear();
        _staticDram.clear();
        _paramValues.clear();
    }

    // -----------------------------------------------------------------------
    // Parameter access
    // -----------------------------------------------------------------------

    void setParameter(int index, int16_t value) {
        if (index < 0 || index >= (int)_paramValues.size()) return;
        _paramValues[index] = value;
        if (_factory && _factory->parameterChanged) {
            _factory->parameterChanged(_algorithm, index);
        }
    }

    int16_t getParameter(int index) const {
        if (index < 0 || index >= (int)_paramValues.size()) return 0;
        return _paramValues[index];
    }

    int numParameters() const { return (int)_paramValues.size(); }

    /// Access the raw algorithm pointer (for plugin-specific test helpers).
    _NT_algorithm* getAlgorithm() { return _algorithm; }

    // -----------------------------------------------------------------------
    // Audio processing
    // -----------------------------------------------------------------------

    /// Number of buses in the disting NT bus system.
    static constexpr int NUM_BUSES = 28;

    /// Run one step() call.  numFrames must be a multiple of 4.
    /// busFrames is allocated internally and zeroed before each call.
    /// Returns pointer to the full bus buffer (NUM_BUSES × numFrames floats).
    float* step(int numFrames) {
        prepareStep(numFrames);
        return executeStep(numFrames);
    }

    /// Allocate and zero the bus buffer in preparation for a step.
    /// Use this + fillBus() + executeStep() to inject CV/audio inputs.
    void prepareStep(int numFrames) {
        int totalFloats = NUM_BUSES * numFrames;
        if ((int)_busBuffer.size() < totalFloats) {
            _busBuffer.resize(totalFloats, 0.0f);
        }
        std::memset(_busBuffer.data(), 0, totalFloats * sizeof(float));
    }

    /// Write sample data into a bus before executeStep().
    /// busIndex is 0-based.
    void fillBus(int busIndex, const float* data, int numFrames) {
        float* dst = _busBuffer.data() + busIndex * numFrames;
        std::memcpy(dst, data, numFrames * sizeof(float));
    }

    /// Execute the plugin's step function on the current bus buffer (no zeroing).
    /// Call prepareStep() first, optionally fillBus(), then this.
    float* executeStep(int numFrames) {
        if (_factory && _factory->step && _algorithm) {
            _factory->step(_algorithm, _busBuffer.data(), numFrames / 4);
        }
        return _busBuffer.data();
    }

    /// Convenience: get pointer to a specific bus after step().
    float* getBus(int busIndex, int numFrames) {
        return _busBuffer.data() + busIndex * numFrames;
    }

    // -----------------------------------------------------------------------
    // MIDI
    // -----------------------------------------------------------------------

    void midiMessage(uint8_t status, uint8_t data1, uint8_t data2) {
        if (_factory && _factory->midiMessage && _algorithm) {
            _factory->midiMessage(_algorithm, status, data1, data2);
        }
    }

    void midiNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        midiMessage(0x90 | (channel & 0x0F), note, velocity);
    }

    void midiNoteOff(uint8_t channel, uint8_t note) {
        midiMessage(0x80 | (channel & 0x0F), note, 0);
    }

    void midiCC(uint8_t channel, uint8_t cc, uint8_t value) {
        midiMessage(0xB0 | (channel & 0x0F), cc, value);
    }

    void midiPitchBend(uint8_t channel, int value14bit) {
        // value14bit: 0-16383, center 8192
        uint8_t lsb = value14bit & 0x7F;
        uint8_t msb = (value14bit >> 7) & 0x7F;
        midiMessage(0xE0 | (channel & 0x0F), lsb, msb);
    }

    void midiAftertouch(uint8_t channel, uint8_t pressure) {
        midiMessage(0xD0 | (channel & 0x0F), pressure, 0);
    }

    void midiRealtime(uint8_t byte) {
        if (_factory && _factory->midiRealtime && _algorithm) {
            _factory->midiRealtime(_algorithm, byte);
        }
    }

    void midiClockStart()    { midiRealtime(0xFA); }
    void midiClockContinue() { midiRealtime(0xFB); }
    void midiClockStop()     { midiRealtime(0xFC); }
    void midiClockTick()     { midiRealtime(0xF8); }

    void midiPolyAftertouch(uint8_t channel, uint8_t note, uint8_t pressure) {
        midiMessage(0xA0 | (channel & 0x0F), note, pressure);
    }

    // -----------------------------------------------------------------------
    // Info
    // -----------------------------------------------------------------------

    const _NT_factory* factory() const { return _factory; }
    _NT_algorithm* algorithm() const { return _algorithm; }

    const char* name() const {
        return _factory ? _factory->name : "(not loaded)";
    }

    // -----------------------------------------------------------------------
    // Audio analysis helpers
    // -----------------------------------------------------------------------

    /// Compute RMS of a buffer.
    static float rms(const float* buffer, int numSamples) {
        double sum = 0.0;
        for (int i = 0; i < numSamples; ++i) {
            sum += (double)buffer[i] * (double)buffer[i];
        }
        return (float)std::sqrt(sum / numSamples);
    }

    /// Compute peak absolute value.
    static float peak(const float* buffer, int numSamples) {
        float p = 0.0f;
        for (int i = 0; i < numSamples; ++i) {
            float a = std::fabs(buffer[i]);
            if (a > p) p = a;
        }
        return p;
    }

    /// Check if buffer is silence (all samples below threshold).
    static bool isSilent(const float* buffer, int numSamples, float threshold = 1e-6f) {
        return peak(buffer, numSamples) < threshold;
    }

    // -----------------------------------------------------------------------
    // Custom UI
    // -----------------------------------------------------------------------

    /// Query which controls the plugin overrides (bitmask of _NT_controls).
    uint32_t hasCustomUi() {
        if (_factory && _factory->hasCustomUi && _algorithm)
            return _factory->hasCustomUi(_algorithm);
        return 0;
    }

    /// Call setupUi — plugin writes current pot positions for soft-takeover.
    void callSetupUi(float (&pots)[3]) {
        if (_factory && _factory->setupUi && _algorithm)
            _factory->setupUi(_algorithm, pots);
    }

    /// Send a UI event.  Constructs a _NT_uiData and calls customUi().
    /// `controls` is a bitmask of _NT_controls that changed.
    /// `pots` are current pot positions [0.0–1.0] for L, C, R.
    /// `encoders` are encoder deltas (±1) for L, R.
    /// `lastButtons` is the previous button state (for edge detection).
    void sendUiEvent(uint16_t controls,
                     float potL = 0.0f, float potC = 0.0f, float potR = 0.0f,
                     int8_t encoderL = 0, int8_t encoderR = 0,
                     uint16_t lastButtons = 0) {
        if (!_factory || !_factory->customUi || !_algorithm) return;
        _NT_uiData data = {};
        data.pots[0]     = potL;
        data.pots[1]     = potC;
        data.pots[2]     = potR;
        data.controls    = controls;
        data.lastButtons = lastButtons;
        data.encoders[0] = encoderL;
        data.encoders[1] = encoderR;
        _factory->customUi(_algorithm, data);
    }

    /// Call draw() via the factory.  Returns the draw() return value.
    /// The plugin renders to the global NT_screen buffer.
    bool callDraw() {
        if (_factory && _factory->draw && _algorithm)
            return _factory->draw(_algorithm);
        return false;
    }

private:
    const _NT_factory*          _factory = nullptr;
    _NT_algorithm*              _algorithm = nullptr;

    _NT_staticRequirements      _staticReq = {};
    _NT_staticMemoryPtrs        _staticPtrs = {};
    _NT_algorithmRequirements   _req = {};
    _NT_algorithmMemoryPtrs     _ptrs = {};

    std::vector<uint8_t>        _staticDram;
    std::vector<uint8_t>        _sram;
    std::vector<uint8_t>        _dram;
    std::vector<uint8_t>        _dtc;
    std::vector<uint8_t>        _itc;
    std::vector<int16_t>        _paramValues;
    std::vector<float>          _busBuffer;

    static uint8_t* align8(uint8_t* p) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(p);
        uintptr_t aligned = (addr + 7) & ~(uintptr_t)7;
        return reinterpret_cast<uint8_t*>(aligned);
    }
};
