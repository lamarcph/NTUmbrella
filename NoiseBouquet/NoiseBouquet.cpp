// =============================================================================
// NoiseBouquet.cpp  —  Disting NT port of Befaco's Noise Plethora (VCV edition)
// =============================================================================
// Multi-algorithm via Bank + Program selectors:
//   - 4 banks × up to 10 programs.
//   - One placement-new arena (sized at compile time to fit the largest
//     algorithm in PluginRegistry) hosts the active algo.
//   - On Bank/Program change: in-place destruct, placement-new, init() —
//     all synchronous; a short fade-in/-out in step() suppresses the
//     inevitable click.
//   - X/Y controls glide over a short one-pole ramp to suppress zippering.
// =============================================================================

#include <math.h>
#include <new>
#include <cstring>
#include <distingnt/api.h>

#include "include/nt_rack_shim.hpp"
#include "include/PluginRegistry.hpp"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

// =============================================================================
// operator new / delete stubs
// =============================================================================
// The Disting NT plugin loader does not provide the C++ runtime's heap
// operators. We never allocate on the heap (placement-new only) and we never
// call `delete` on polymorphic algos (destruction goes through explicit
// `~T()` in PluginRegistry::unmake), but the compiler still emits a
// "deleting destructor" entry in the vtable of any class with a virtual
// destructor (e.g. NoisePlethoraPlugin). That deleting destructor references
// `operator delete(void*, size_t)` — the missing `_Zdlpvj` symbol on the
// 32-bit ARM target where size_t == unsigned int.
//
// Providing no-op stubs satisfies the linker. They are never actually
// invoked at runtime; if they ever were, that would be a bug (heap usage
// from real-time audio code).
//
// Same pattern used by Oneiroi/include/Oneiroi/Patch.h.
// =============================================================================
void operator delete(void*) noexcept              {}
void operator delete[](void*) noexcept            {}
void operator delete(void*, std::size_t) noexcept {}
void operator delete[](void*, std::size_t) noexcept {}

// =============================================================================
// Parameters
// =============================================================================

enum {
    kParamOut,
    kParamOutMode,
    kParamBank,
    kParamProgram,
    kParamX,
    kParamY,
    kParamGain,
    kNumParams,
};

// Defaults: Bank 4, Program 2 = WhiteNoise (the M1-equivalent default sound).
static constexpr int kDefaultBank    = 4;
static constexpr int kDefaultProgram = 2;

static const _NT_parameter parameters[] = {
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Out", 1, 13)
    { .name = "Bank",    .min = 1, .max = 4,    .def = kDefaultBank,
      .unit = kNT_unitHasStrings, .scaling = 0,                .enumStrings = nullptr },
    { .name = "Program", .min = 1, .max = 10,   .def = kDefaultProgram,
      .unit = kNT_unitHasStrings, .scaling = 0,                .enumStrings = nullptr },
    { .name = "X",       .min = 0, .max = 10000, .def = 5000,
      .unit = kNT_unitPercent,    .scaling = kNT_scaling100,   .enumStrings = nullptr },
    { .name = "Y",       .min = 0, .max = 10000, .def = 5000,
      .unit = kNT_unitPercent,    .scaling = kNT_scaling100,   .enumStrings = nullptr },
    { .name = "Gain",    .min = 0, .max = 2000, .def = 1000,
      .unit = kNT_unitPercent,    .scaling = kNT_scaling1000,  .enumStrings = nullptr },
};
static_assert(ARRAY_SIZE(parameters) == kNumParams, "parameter count mismatch");

static const uint8_t pagePatch[]   = { kParamBank, kParamProgram, kParamX, kParamY, kParamGain };
static const uint8_t pageRouting[] = { kParamOut, kParamOutMode };

static const _NT_parameterPage pages[] = {
    { .name = "Patch",   .numParams = ARRAY_SIZE(pagePatch),   .params = pagePatch   },
    { .name = "Routing", .numParams = ARRAY_SIZE(pageRouting), .params = pageRouting },
};

static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages),
    .pages    = pages,
};

// =============================================================================
// Algorithm instance
// =============================================================================

// A short program-change crossfade (ms) suppresses the click from
// re-initialising the algo arena. Synchronous construction stays well within
// audio block timing for the small Bank-4 algos.
static constexpr float kFadeMs = 30.0f;
// X/Y smoothing (ms) trims audible zippering when the user moves controls.
static constexpr float kControlSmoothMs = 10.0f;

struct _NPAlgorithm_DTC {
    // Pointer to the live algorithm arena (lives in DRAM, see construct()).
    // The arena is sized at compile time to fit the largest entry in the
    // registry; alignment is the strictest required by any algo.
    // We keep the arena in DRAM (not DTC) because DTC is a small scarce
    // resource shared across loaded algorithms — putting the ~30 kB arena
    // there has been observed to hang the host on plugin load.
    uint8_t* algoArena = nullptr;
    NoisePlethoraPlugin* algo = nullptr;
    const np_registry::Entry* entry = nullptr; // current registry row (or nullptr)

    // Target control values come from parameterChanged(); kx/ky glide toward
    // them in step() to suppress zippering without changing steady-state.
    float targetKx = 0.5f;
    float targetKy = 0.5f;
    float kx       = 0.5f;
    float ky       = 0.5f;
    float gain = 1.0f;
    float entryGain = 1.0f;  // per-program trim from registry

    // Tracks the (bank, slot) currently constructed in algoArena so we only
    // reconstruct when the user actually moves the encoder.
    uint8_t curBank = 0;
    uint8_t curSlot = 0;

    // Crossfade state machine.
    enum FadeState : uint8_t { kRunning, kFadingOut, kFadingIn };
    FadeState fadeState   = kRunning;
    int       fadeSamples = 0;   // total samples in a fade segment
    int       fadePos     = 0;   // sample index within current segment
    uint8_t   targetBank  = 0;   // pending switch target
    uint8_t   targetSlot  = 0;
    bool      switchPending = false;

    // Linear-interpolation resampler state. The vendored algos run at a
    // fixed internal rate of 44.1 kHz (AUDIO_SAMPLE_RATE_EXACT); when the
    // NT host runs at any other rate we resample on the fly so pitches and
    // times match the original Befaco hardware. Bypassed when rates match.
    float resamplePhase = 0.0f;  // 0..1, fractional position into [prev,curr]
    float resamplePrev  = 0.0f;
    float resampleCurr  = 0.0f;
    bool  resampleInited = false;
};

struct _NPAlgorithm : public _NT_algorithm {
    _NPAlgorithm(_NPAlgorithm_DTC* dtc_) : dtc(dtc_) {}
    ~_NPAlgorithm() {}
    _NPAlgorithm_DTC* dtc;
};

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

static void destructAlgo(_NPAlgorithm_DTC* dtc) {
    if (dtc->algo && dtc->entry) {
        dtc->entry->destroy(dtc->algo);
    }
    dtc->algo  = nullptr;
    dtc->entry = nullptr;
}

// Build the algorithm at (bank, slot), or leave the arena empty if no entry.
static void constructAlgo(_NPAlgorithm_DTC* dtc, uint8_t bank, uint8_t slot) {
    destructAlgo(dtc);
    const auto* e = np_registry::find(bank, slot);
    dtc->curBank = bank;
    dtc->curSlot = slot;
    dtc->entry   = e;
    // Reset resampler so the new algo starts from a clean state.
    dtc->resamplePhase  = 0.0f;
    dtc->resamplePrev   = 0.0f;
    dtc->resampleCurr   = 0.0f;
    dtc->resampleInited = false;
    if (!e) {
        dtc->entryGain = 1.0f;
        return;          // leave arena dormant; step() will emit silence
    }
    dtc->algo = e->factory(dtc->algoArena);
    if (dtc->algo) {
        dtc->algo->init();
        dtc->algo->process(dtc->kx, dtc->ky);
    }
    dtc->entryGain = e->gain;
}

// =============================================================================
// NT factory callbacks
// =============================================================================

static void calculateRequirements(_NT_algorithmRequirements& req,
                                  const int32_t* /*specifications*/) {
    req.numParameters = kNumParams;
    req.sram          = sizeof(_NPAlgorithm);
    // Algo arena lives in DRAM. Reserve enough headroom to align the base
    // pointer up to the strictest algorithm alignment.
    req.dram          = np_registry::kMaxAlgoSize + np_registry::kMaxAlgoAlign;
    req.dtc           = sizeof(_NPAlgorithm_DTC);
    req.itc           = 0;
}

static _NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                                const _NT_algorithmRequirements& /*req*/,
                                const int32_t* /*specifications*/) {
    nt_shim::setSampleRate((float)NT_globals.sampleRate);

    auto* dtc = new (ptrs.dtc) _NPAlgorithm_DTC();
    auto* alg = new (ptrs.sram) _NPAlgorithm(dtc);

    // Carve the algo arena out of the DRAM block, aligned up to the strictest
    // algorithm alignment. calculateRequirements() reserved kMaxAlgoSize +
    // kMaxAlgoAlign bytes specifically so this round-up always fits.
    {
        uintptr_t raw   = reinterpret_cast<uintptr_t>(ptrs.dram);
        uintptr_t align = (uintptr_t)np_registry::kMaxAlgoAlign;
        uintptr_t aligned = (raw + align - 1) & ~(align - 1);
        dtc->algoArena = reinterpret_cast<uint8_t*>(aligned);
    }

    constructAlgo(dtc, kDefaultBank, kDefaultProgram);

    alg->parameters     = parameters;
    alg->parameterPages = &parameterPages;
    return alg;
}

static void parameterChanged(_NT_algorithm* self, int p) {
    auto* a   = static_cast<_NPAlgorithm*>(self);
    auto* dtc = a->dtc;
    switch (p) {
        case kParamX:    dtc->targetKx = a->v[p] * 0.0001f; break;
        case kParamY:    dtc->targetKy = a->v[p] * 0.0001f; break;
        case kParamGain: dtc->gain = a->v[p] * 0.001f; break;
        case kParamBank:
        case kParamProgram: {
            uint8_t b = (uint8_t)a->v[kParamBank];
            uint8_t s = (uint8_t)a->v[kParamProgram];
            // Only react if it's actually a change vs what's currently
            // constructed — avoids destruct/construct churn while the host
            // initialises every parameter on load.
            if (b != dtc->curBank || s != dtc->curSlot) {
                dtc->targetBank    = b;
                dtc->targetSlot    = s;
                dtc->switchPending = true;
                if (dtc->fadeState == _NPAlgorithm_DTC::kRunning) {
                    dtc->fadeState   = _NPAlgorithm_DTC::kFadingOut;
                    // Fade duration is in NT output frames, so use the host
                    // sample rate (not the algo's fixed 44.1 kHz).
                    dtc->fadeSamples = (int)(kFadeMs * 0.001f
                                             * (float)NT_globals.sampleRate);
                    if (dtc->fadeSamples < 4) dtc->fadeSamples = 4;
                    dtc->fadePos     = 0;
                }
            }
            break;
        }
        default: break;
    }
}

// One-sample render from the active algorithm; returns 0 when no algo is
// constructed (empty bank/slot).
static inline float pullSample(_NPAlgorithm_DTC* dtc) {
    if (!dtc->algo) return 0.0f;
    return dtc->algo->processGraph();
}

// Resampling pull: advances the 44.1 kHz source by `ratio` per output frame
// and returns a linearly-interpolated sample. Used when the NT host runs at
// a sample rate other than 44.1 kHz. `ratio = 44100 / hostRate`.
static inline float pullSampleResampled(_NPAlgorithm_DTC* dtc, float ratio) {
    if (!dtc->algo) {
        // Keep state consistent so a re-enabled algo doesn't pop.
        dtc->resampleInited = false;
        return 0.0f;
    }
    if (!dtc->resampleInited) {
        dtc->resamplePrev   = dtc->algo->processGraph();
        dtc->resampleCurr   = dtc->algo->processGraph();
        dtc->resamplePhase  = 0.0f;
        dtc->resampleInited = true;
    }
    dtc->resamplePhase += ratio;
    while (dtc->resamplePhase >= 1.0f) {
        dtc->resamplePhase -= 1.0f;
        dtc->resamplePrev = dtc->resampleCurr;
        dtc->resampleCurr = dtc->algo->processGraph();
    }
    return dtc->resamplePrev
         + (dtc->resampleCurr - dtc->resamplePrev) * dtc->resamplePhase;
}

static void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    auto* a   = static_cast<_NPAlgorithm*>(self);
    auto* dtc = a->dtc;

    const int numFrames = numFramesBy4 * 4;

    // Host sample rate; fade/smoothing time constants are scaled against it
    // because they apply per output frame in this loop.
    const float hostSR = (float)NT_globals.sampleRate;

    // One-pole smoothing per audio block is enough to suppress zipper noise
    // while keeping encoder response direct.
    const float smoothSamples = kControlSmoothMs * 0.001f * hostSR;
    const float alpha = (smoothSamples > 1.0f)
                      ? (float)numFrames / ((float)numFrames + smoothSamples)
                      : 1.0f;
    dtc->kx += (dtc->targetKx - dtc->kx) * alpha;
    dtc->ky += (dtc->targetKy - dtc->ky) * alpha;

    // Push current X/Y to the algo (cheap; algos cache derived values).
    if (dtc->algo) dtc->algo->process(dtc->kx, dtc->ky);
    const int   outBus    = a->v[kParamOut];
    const bool  replace   = a->v[kParamOutMode] != 0;
    const float baseGain  = dtc->gain * dtc->entryGain;

    if (outBus < 1) return;
    float* out = busFrames + (outBus - 1) * numFrames;

    // Resampling ratio: how many 44.1 kHz source samples advance per host
    // output frame. When the host already runs at 44.1 kHz we bypass the
    // resampler entirely (hot path is one pullSample per output frame).
    const float ratio = 44100.0f / hostSR;
    const bool  resample = (ratio < 0.9999f) || (ratio > 1.0001f);

    for (int i = 0; i < numFrames; ++i) {
        // Crossfade envelope: 1.0 in kRunning, ramp 1->0 in kFadingOut,
        // ramp 0->1 in kFadingIn, then back to kRunning.
        float fade = 1.0f;
        switch (dtc->fadeState) {
            case _NPAlgorithm_DTC::kRunning:
                break;
            case _NPAlgorithm_DTC::kFadingOut: {
                fade = 1.0f - (float)dtc->fadePos / (float)dtc->fadeSamples;
                if (++dtc->fadePos >= dtc->fadeSamples) {
                    // Switch happens here: synchronous destruct + construct
                    // + init() of the new algo.
                    if (dtc->switchPending) {
                        constructAlgo(dtc, dtc->targetBank, dtc->targetSlot);
                        dtc->switchPending = false;
                    }
                    dtc->fadeState = _NPAlgorithm_DTC::kFadingIn;
                    dtc->fadePos   = 0;
                }
                break;
            }
            case _NPAlgorithm_DTC::kFadingIn: {
                fade = (float)dtc->fadePos / (float)dtc->fadeSamples;
                if (++dtc->fadePos >= dtc->fadeSamples) {
                    dtc->fadeState = _NPAlgorithm_DTC::kRunning;
                }
                break;
            }
        }

        const float raw = resample ? pullSampleResampled(dtc, ratio)
                                   : pullSample(dtc);
        const float s = raw * baseGain * fade;
        if (replace) out[i] = s;
        else         out[i] += s;
    }
}

// Bank/Program parameter strings: shows the algorithm name (or "--" for
// empty slots) so the user sees what's loaded without having to memorise
// the layout.
static int parameterString(_NT_algorithm* self, int p, int v, char* buff) {
    if (p == kParamBank) {
        static const char* const names[] = { "Bank 1", "Bank 2", "Bank 3", "Bank 4" };
        if (v < 1 || v > 4) return 0;
        const char* s = names[v - 1];
        size_t n = std::strlen(s);
        std::memcpy(buff, s, n + 1);
        return (int)n;
    }
    if (p == kParamProgram) {
        auto* a = static_cast<_NPAlgorithm*>(self);
        uint8_t b = (uint8_t)a->v[kParamBank];
        const auto* e = np_registry::find(b, (uint8_t)v);
        const char* s = e ? e->name : "--";
        size_t n = std::strlen(s);
        if (n >= kNT_parameterStringSize) n = kNT_parameterStringSize - 1;
        std::memcpy(buff, s, n);
        buff[n] = '\0';
        return (int)n;
    }
    return 0;
}

// =============================================================================
// Factory + plugin entry
// =============================================================================

static const _NT_factory factory = {
    .guid                        = NT_MULTICHAR('N','B','q','t'),
    .name                        = "Noise Bouquet",
    .description                 = "Disting NT port of Befaco's Noise Plethora (VCV Rack edition)",
    .numSpecifications           = 0,
    .specifications              = nullptr,
    .calculateStaticRequirements = nullptr,
    .initialise                  = nullptr,
    .calculateRequirements       = calculateRequirements,
    .construct                   = construct,
    .parameterChanged            = parameterChanged,
    .step                        = step,
    .draw                        = nullptr,
    .midiRealtime                = nullptr,
    .midiMessage                 = nullptr,
    .tags                        = kNT_tagInstrument,
    .hasCustomUi                 = nullptr,
    .customUi                    = nullptr,
    .setupUi                     = nullptr,
    .serialise                   = nullptr,
    .deserialise                 = nullptr,
    .midiSysEx                   = nullptr,
    .parameterUiPrefix           = nullptr,
    .parameterString             = parameterString,
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
        case kNT_selector_version:      return kNT_apiVersionCurrent;
        case kNT_selector_numFactories: return 1;
        case kNT_selector_factoryInfo:
            if (data == 0) return (uintptr_t)&factory;
            return 0;
        default: return 0;
    }
}
