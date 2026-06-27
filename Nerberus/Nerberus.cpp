#include <cmath>
#include <new>
#include <distingnt/api.h>

#include "CheapMaths.h"
#include "CompressedGritEngine.h"
#include "LFO.h"
#include "PeakFollower.h"
#include "Polyphase.h"
#include "ShapedADSR.h"
#include "ZDFFilter.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

static constexpr int kNumBuses = 28;
static constexpr int kBlockChunk = 64;

// --- LR4 3-band crossover ---
struct BiquadCoeffs {
    float b0 = 0.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
};

struct BiquadDF2T {
    float s1 = 0.f, s2 = 0.f;
    float process(float x, const BiquadCoeffs& c) {
        float y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

struct LR4Filter {
    BiquadDF2T st1, st2;
    float process(float x, const BiquadCoeffs& c) {
        return st2.process(st1.process(x, c), c);
    }
};

static void calcLR4LP(float fc, float fs, BiquadCoeffs& c) {
    const float k    = std::tan(3.14159265f * fc / fs);
    const float Q    = 0.70710678f;
    const float norm = 1.0f / (1.0f + k / Q + k * k);
    c.b0 =  k * k * norm;
    c.b1 =  2.0f * c.b0;
    c.b2 =  c.b0;
    c.a1 =  2.0f * (k * k - 1.0f) * norm;
    c.a2 = (1.0f - k / Q + k * k) * norm;
}

static void calcLR4HP(float fc, float fs, BiquadCoeffs& c) {
    const float k    = std::tan(3.14159265f * fc / fs);
    const float Q    = 0.70710678f;
    const float norm = 1.0f / (1.0f + k / Q + k * k);
    c.b0 =  norm;
    c.b1 = -2.0f * norm;
    c.b2 =  norm;
    c.a1 =  2.0f * (k * k - 1.0f) * norm;
    c.a2 = (1.0f - k / Q + k * k) * norm;
}

struct BandSplitter {
    LR4Filter loLpL, loLpR;  // low-band  LP @ fcLo
    LR4Filter loHpL, loHpR;  // mid+hi    HP @ fcLo
    LR4Filter hiLpL, hiLpR;  // mid-band  LP @ fcHi
    LR4Filter hiHpL, hiHpR;  // hi-band   HP @ fcHi
    BiquadCoeffs lpLo, hpLo, lpHi, hpHi;
    float cachedFcLo = -1.f, cachedFcHi = -1.f;

    void updateCoeffs(float fcLo, float fcHi, float sampleRate) {
        if (fcLo != cachedFcLo) {
            calcLR4LP(fcLo, sampleRate, lpLo);
            calcLR4HP(fcLo, sampleRate, hpLo);
            cachedFcLo = fcLo;
        }
        if (fcHi != cachedFcHi) {
            calcLR4LP(fcHi, sampleRate, lpHi);
            calcLR4HP(fcHi, sampleRate, hpHi);
            cachedFcHi = fcHi;
        }
    }

    void split(float inL, float inR,
               float& outLoL, float& outMidL, float& outHiL,
               float& outLoR, float& outMidR, float& outHiR) {
        outLoL          = loLpL.process(inL, lpLo);
        const float mhL = loHpL.process(inL, hpLo);
        outMidL         = hiLpL.process(mhL, lpHi);
        outHiL          = hiHpL.process(mhL, hpHi);

        outLoR          = loLpR.process(inR, lpLo);
        const float mhR = loHpR.process(inR, hpLo);
        outMidR         = hiLpR.process(mhR, lpHi);
        outHiR          = hiHpR.process(mhR, hpHi);
    }
};

struct _NerberusAlgorithm_DTC {
    CompressedGritEngine engine;
    ZDFFilter filterL, filterR;
    ZDFFilter filterL2x, filterR2x;
    ZDFFilter filterL4x, filterR4x;
    Polyphase2xEngine filterOS2xL, filterOS2xR;
    Polyphase4xEngine filterOS4xL, filterOS4xR;
    LFO ringCarrier;
    BandSplitter splitter;

    // Envelope follower for overall drive / filter / bias modulation
    PeakFollower envFollower;    // per-sample one-pole peak tracker

    // Per-band transient envelope followers (ShapedADSR — gate-driven, kept for transient shaper)
    ShapedADSR envFastLo,  envSlowLo;
    ShapedADSR envFastMid, envSlowMid;
    ShapedADSR envFastHi,  envSlowHi;

    float sampleRate = 48000.f;

    // Engine / global
    float drive = 0.3f;  // stored as t^1.5 where t=0.45 (see parameterChanged)
    float pressLo = 0.0f, pressMid = 0.0f, pressHi = 0.0f;
    float gritLo = 0.0f, gritMid = 0.0f, gritHi = 0.0f;
    float mix = 1.0f;
    float outputGain = 1.0f;

    // Crossover frequencies
    float crossFreqLo = 250.0f;
    float crossFreqHi = 3000.0f;

    // Filter (full-band, post-recombine)
    float filterCutoff = 20000.0f;
    float filterResonance = 0.0f;
    float filterDrive = 1.0f;
    int filterOversample = 0; // 0=1x, 1=2x, 2=4x

    // Filter envelope follower amounts (range -1..+1, applied via envDriveFollower)
    float filterCutoffEnv = 0.0f;
    float filterDriveEnv  = 0.0f;
    float filterResEnv    = 0.0f;

    // CV modulation
    float ringFreqHz        = 1000.0f;   // base ring freq for CV modulation
    float cvFilterFreqDepth = 1.0f;      // oct/V for filter freq CV
    float cvRingFreqDepth   = 1.0f;      // oct/V for ring freq CV

    // Per-band crush + decimation state
    int bitDepthLo = 16,   bitDepthMid = 16,   bitDepthHi = 16;
    int decimLo = 1,       decimMid = 1,        decimHi = 1;
    int decimPhaseLo = 0,  decimPhaseMid = 0,   decimPhaseHi = 0;
    float decimHoldLoL = 0.f,  decimHoldLoR = 0.f;
    float decimHoldMidL = 0.f, decimHoldMidR = 0.f;
    float decimHoldHiL = 0.f,  decimHoldHiR = 0.f;

    // Per-band noise
    float noiseLevelLo = 0.f, noiseLevelMid = 0.f, noiseLevelHi = 0.f;
    int noiseColor = 0;
    float pink0 = 0.f, pink1 = 0.f, pink2 = 0.f, lofiState = 0.f;

    // Per-band ring modulator depth
    float ringDepthLo = 0.f, ringDepthMid = 0.f, ringDepthHi = 0.f;

    // Per-band stereo width
    float widthLo = 1.f, widthMid = 1.f, widthHi = 1.f;

    // Per-band transient shaper
    float transientAttackLo  = 0.f, transientAttackMid  = 0.f, transientAttackHi  = 0.f;
    float transientSustainLo = 0.f, transientSustainMid = 0.f, transientSustainHi = 0.f;
    float envSensitivity = 0.0f;

    // Envelope follower ballistics (exposed as UI params)
    float envAttackMs  = 0.2f;   // ms — very small triggers instant-snap logic
    float envReleaseMs = 20.0f;  // ms
    float envShape     = -0.5f;  // -1..+1

    // Input conditioning: high-pass before crossover split
    float inputHPFreq  = 20.0f;  // Hz — 20 Hz = transparent
    float inputHPCoeff = 0.0f;   // one-pole LP coefficient: exp(-2pi*fc/fs)
    float hpStateL     = 0.0f;   // LP filter state for L channel HP
    float hpStateR     = 0.0f;   // LP filter state for R channel HP

    // Dynamic bias: envDrive-driven DC offset before saturation → even-order harmonics
    float biasAmount   = 0.0f;   // 0..1, scaled from 0..1000 param

    // Output LP (cab rolloff): one-pole LP on the recombined wet signal
    // Default 20000 Hz = transparent (step() skips when freq == 20000).
    float outputLPFreq  = 20000.0f;
    float outputLPCoeff = 0.0f;    // exp(-2pi*fc/fs)
    float lpOutStateL   = 0.0f;
    float lpOutStateR   = 0.0f;

    // Output soft limiter: cheap_saturate-based blend ceiling
    float outLimiter    = 0.0f;   // 0..1, scaled from 0..1000 param

    // Lo-band saturation mode: 0=symmetric (default), 1=asymmetric (even-order)
    int   loSatMode     = 0;

    // Temp buffers for block-level filter oversampling.
    // Kept in DTC rather than on the stack to avoid overflowing the audio thread's
    // limited stack budget (step() already uses ~2.5 KB of stack buffers).
    // Sized for 4x OS at max block size; 2x uses only the first n*2 elements.
    float filterOsBufL[kBlockChunk * 4];
    float filterOsBufR[kBlockChunk * 4];

    // Pre-computed ring carrier buffer — one LFO call per block instead of per-sample.
    float ringCarrierBuf[kBlockChunk];
};

struct _NerberusAlgorithm : public _NT_algorithm {
    explicit _NerberusAlgorithm(_NerberusAlgorithm_DTC* dtc_) : dtc(dtc_) {}
    _NerberusAlgorithm_DTC* dtc;
};

enum {
    kParamInL = 0, kParamInR,
    kParamOutL, kParamOutLMode,
    kParamOutR, kParamOutRMode,

    // Engine page
    kParamCrossLo, kParamCrossHi,
    kParamDrive,
    kParamPressLo, kParamPressMid, kParamPressHi,
    kParamMix, kParamOutput,

    // Filter page
    kParamFilterModel, kParamFilterMode,
    kParamFilterCutoff, kParamFilterRes, kParamFilterDrive,
    kParamFilterOversample,

    // Crush + Noise page
    kParamBitDepthLo, kParamBitDepthMid, kParamBitDepthHi,
    kParamDecimationLo, kParamDecimationMid, kParamDecimationHi,
    kParamNoiseLevelLo, kParamNoiseLevelMid, kParamNoiseLevelHi,
    kParamNoiseColor,

    // Ring + Width page
    kParamRingFreq,
    kParamRingDepthLo, kParamRingDepthMid, kParamRingDepthHi,
    kParamWidthLo, kParamWidthMid, kParamWidthHi,

    // Transient page
    kParamEnvSensitivity,
    kParamEnvAttack, kParamEnvRelease, kParamEnvShape,
    kParamTransientAttackLo,  kParamTransientAttackMid,  kParamTransientAttackHi,
    kParamTransientSustainLo, kParamTransientSustainMid, kParamTransientSustainHi,

    // Per-band grit (harmonic texture per band)
    kParamGritLo, kParamGritMid, kParamGritHi,

    // Filter envelope follower amounts
    kParamFilterCutoffEnv, kParamFilterDriveEnv, kParamFilterResEnv,

    // CV modulation inputs
    kParamCVFilterFreqIn, kParamCVFilterFreqDepth,
    kParamCVRingFreqIn,   kParamCVRingFreqDepth,

    // Input conditioning + dynamic bias (appended last to avoid index shifts)
    kParamInputHP,
    kParamBias,

    // Output shaping
    kParamOutputLP,
    kParamOutLimiter,
    kParamLoSatMode,

    kNumParams
};

static char const * const enumStringsFilterModel[] = {
    "SVF", "Ladder", "MS20", "Diode"
};

static char const * const enumStringsFilterMode[] = {
    "LP 2-pole", "LP 4-pole", "HP 2-pole", "BP 2-pole", "Notch", "HP+LP", "Bypass"
};

static char const * const enumStringsFilterOS[] = {
    "1x", "2x", "4x"
};

static char const * const enumStringsNoiseColor[] = {
    "White", "Pink", "Lo-Fi"
};

static const _NT_parameter parameters[] = {
    NT_PARAMETER_AUDIO_INPUT("In L", 1, 1)
    NT_PARAMETER_AUDIO_INPUT("In R", 1, 2)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Out L", 1, 13)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Out R", 1, 14)
    // Engine
    { "XO Lo Hz",   20,   2000,  250, kNT_unitHz,      0,               nullptr },
    { "XO Hi Hz",  200,   8000, 3000, kNT_unitHz,      0,               nullptr },
    { "Drive",       0,   1000,  450, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Press Lo",  -1000,  1000,    0, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Press Mid", -1000,  1000,    0, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Press Hi",  -1000,  1000,    0, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Mix",         0,   1000, 1000, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Output",      0,   2000, 1000, kNT_unitPercent, kNT_scaling1000, nullptr },

    // Filter
    { "Filter Model",  0, 3,     0, kNT_unitEnum,    0,               enumStringsFilterModel },
    { "Filter Mode",   0, 6,     6, kNT_unitEnum,    0,               enumStringsFilterMode  },
    { "Filter Cutoff", 0, 10000, 10000, kNT_unitNone, 0,               nullptr },
    { "Filter Res",    0, 1000,   0, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Filter Drive",  1000, 10000, 1000, kNT_unitNone, kNT_scaling1000, nullptr },
    { "Filter OS",     0, 2,      0, kNT_unitEnum,    0,               enumStringsFilterOS    },

    // Crush + Noise
    { "Crush Lo",  1, 16, 16, kNT_unitNone, 0, nullptr },
    { "Crush Mid", 1, 16, 16, kNT_unitNone, 0, nullptr },
    { "Crush Hi",  1, 16, 16, kNT_unitNone, 0, nullptr },
    { "Decim Lo",  1, 64,  1, kNT_unitNone, 0, nullptr },
    { "Decim Mid", 1, 64,  1, kNT_unitNone, 0, nullptr },
    { "Decim Hi",  1, 64,  1, kNT_unitNone, 0, nullptr },
    { "Noise Lo",  0, 1000, 0, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Noise Mid", 0, 1000, 0, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Noise Hi",  0, 1000, 0, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Noise Color", 0, 2, 0, kNT_unitEnum, 0, enumStringsNoiseColor },

    // Ring + Width
    { "Ring Freq",  20, 10000, 1000, kNT_unitHz,      0,               nullptr },
    { "Ring Lo",     0,  1000,    0, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Ring Mid",    0,  1000,    0, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Ring Hi",     0,  1000,    0, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Width Lo",    0,  2000, 1000, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Width Mid",   0,  2000, 1000, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Width Hi",    0,  2000, 1000, kNT_unitPercent, kNT_scaling1000, nullptr },

    // Transient
    { "Env Sens",     0,  1000,    0, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Env Attack",   0,  500,     0, kNT_unitMs,      0,               nullptr },
    { "Env Release",  1,  2000,   20, kNT_unitMs,      0,               nullptr },
    { "Env Shape",   -1000, 1000, -500, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Trs Atk Lo", -1000, 1000,    0, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Trs Atk Mid",-1000, 1000,    0, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Trs Atk Hi", -1000, 1000,    0, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Trs Sus Lo", -1000, 1000,    0, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Trs Sus Mid",-1000, 1000,    0, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Trs Sus Hi", -1000, 1000,    0, kNT_unitPercent, kNT_scaling1000, nullptr },

    // Per-band grit
    { "Grit Lo",  0, 1000, 0, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Grit Mid", 0, 1000, 0, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Grit Hi",  0, 1000, 0, kNT_unitPercent, kNT_scaling1000, nullptr },

    // Filter envelope follower amounts
    { "Flt Cutoff Env", -1000, 1000, 0, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Flt Drive Env",  -1000, 1000, 0, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Flt Res Env",    -1000, 1000, 0, kNT_unitPercent, kNT_scaling1000, nullptr },

    // CV modulation inputs
    NT_PARAMETER_CV_INPUT("CV Flt Freq", 1, 0)
    { "CV Flt Depth", -10000, 10000, 1000, kNT_unitNone, kNT_scaling1000, nullptr },
    NT_PARAMETER_CV_INPUT("CV Ring Freq", 1, 0)
    { "CV Ring Depth", -10000, 10000, 1000, kNT_unitNone, kNT_scaling1000, nullptr },

    // Input conditioning + dynamic bias
    { "Input HP",  20, 500,  20, kNT_unitHz,      0,               nullptr },
    { "Bias",       0, 1000,  0, kNT_unitPercent, kNT_scaling1000, nullptr },

    // Output shaping
    { "Out LP",    2000, 20000, 20000, kNT_unitHz,      0,               nullptr },
    { "Out Limit",    0,  1000,     0, kNT_unitPercent, kNT_scaling1000, nullptr },
    { "Lo Asym",      0,     1,     0, kNT_unitNone,    0,               nullptr },
};

static const uint8_t pageRouting[] = {
    kParamInL, kParamInR,
    kParamOutL, kParamOutLMode,
    kParamOutR, kParamOutRMode,
    kParamCVFilterFreqIn,
    kParamCVRingFreqIn
};

static const uint8_t pageEngine[] = {
    kParamCrossLo, kParamCrossHi,
    kParamInputHP, kParamBias,
    kParamDrive, kParamPressLo, kParamPressMid, kParamPressHi,
    kParamLoSatMode,
    kParamGritLo, kParamGritMid, kParamGritHi,
    kParamMix, kParamOutput,
    kParamOutputLP, kParamOutLimiter
};

static const uint8_t pageFilter[] = {
    kParamFilterModel, kParamFilterMode, kParamFilterCutoff, kParamFilterRes, kParamFilterDrive,
    kParamFilterOversample, kParamCVFilterFreqDepth
};

static const uint8_t pageEnvFollower[] = {
    kParamEnvSensitivity,
    kParamEnvAttack, kParamEnvRelease, kParamEnvShape,
    kParamFilterCutoffEnv, kParamFilterDriveEnv, kParamFilterResEnv
};

static const uint8_t pageCrush[] = {
    kParamBitDepthLo, kParamBitDepthMid, kParamBitDepthHi,
    kParamDecimationLo, kParamDecimationMid, kParamDecimationHi,
    kParamNoiseLevelLo, kParamNoiseLevelMid, kParamNoiseLevelHi,
    kParamNoiseColor
};

static const uint8_t pageRingWidth[] = {
    kParamRingFreq, kParamRingDepthLo, kParamRingDepthMid, kParamRingDepthHi,
    kParamCVRingFreqDepth,
    kParamWidthLo, kParamWidthMid, kParamWidthHi
};

static const uint8_t pageTransient[] = {
    kParamTransientAttackLo,  kParamTransientAttackMid,  kParamTransientAttackHi,
    kParamTransientSustainLo, kParamTransientSustainMid, kParamTransientSustainHi
};

static const _NT_parameterPage pages[] = {
    { "Drive",      ARRAY_SIZE(pageEngine),      0, {0, 0}, pageEngine      },
    { "Filter",     ARRAY_SIZE(pageFilter),      0, {0, 0}, pageFilter      },
    { "Env Flwr",   ARRAY_SIZE(pageEnvFollower), 0, {0, 0}, pageEnvFollower },
    { "Crush",      ARRAY_SIZE(pageCrush),       0, {0, 0}, pageCrush       },
    { "Ring/Width", ARRAY_SIZE(pageRingWidth),   0, {0, 0}, pageRingWidth   },
    { "Transient",  ARRAY_SIZE(pageTransient),   0, {0, 0}, pageTransient   },
    { "Routing",    ARRAY_SIZE(pageRouting),     0, {0, 0}, pageRouting     },
};

static const _NT_parameterPages parameterPages = {
    ARRAY_SIZE(pages),
    pages
};

static inline float* busPtr(float* busFrames, int numFrames, int16_t route1Based) {
    if (route1Based <= 0 || route1Based > kNumBuses) return nullptr;
    return busFrames + ((route1Based - 1) * numFrames);
}

static inline const float* busPtr(const float* busFrames, int numFrames, int16_t route1Based) {
    if (route1Based <= 0 || route1Based > kNumBuses) return nullptr;
    return busFrames + ((route1Based - 1) * numFrames);
}

static void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = kNumParams;
    req.sram = sizeof(_NerberusAlgorithm);
    req.dram = 0;
    req.dtc = sizeof(_NerberusAlgorithm_DTC);
    req.itc = 0;
}

static _NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements&, const int32_t*) {
    auto* dtc = new (ptrs.dtc) _NerberusAlgorithm_DTC();
    auto* alg = new (ptrs.sram) _NerberusAlgorithm(dtc);

    const float fs = (float)NT_globals.sampleRate;
    dtc->sampleRate = fs;

    dtc->engine.init(fs);
    dtc->filterL.setSampleRate(fs);
    dtc->filterR.setSampleRate(fs);
    dtc->filterL2x.setSampleRate(fs * 2.0f);
    dtc->filterR2x.setSampleRate(fs * 2.0f);
    dtc->filterL4x.setSampleRate(fs * 4.0f);
    dtc->filterR4x.setSampleRate(fs * 4.0f);
    dtc->filterOS2xL.init(); dtc->filterOS2xR.init();
    dtc->filterOS4xL.init(); dtc->filterOS4xR.init();
    dtc->ringCarrier.setSampleRate(fs);
    dtc->ringCarrier.setShape(LFO::SHAPE_SINE);
    dtc->ringCarrier.setFrequency(dtc->ringFreqHz);

    dtc->splitter.updateCoeffs(dtc->crossFreqLo, dtc->crossFreqHi, fs);

    dtc->envFollower.init(fs);
    dtc->envFollower.setTimes(dtc->envAttackMs * 0.001f, dtc->envReleaseMs * 0.001f);

    // Per-band transient followers – Lo
    dtc->envFastLo.setSampleRate(fs);
    dtc->envFastLo.setParameters(0.0001f, 0.005f, 1.0f, 0.005f);
    dtc->envFastLo.setShape(-0.4f);
    dtc->envSlowLo.setSampleRate(fs);
    dtc->envSlowLo.setParameters(0.005f, 0.08f, 1.0f, 0.08f);
    dtc->envSlowLo.setShape(0.2f);

    // Per-band transient followers – Mid
    dtc->envFastMid.setSampleRate(fs);
    dtc->envFastMid.setParameters(0.0001f, 0.005f, 1.0f, 0.005f);
    dtc->envFastMid.setShape(-0.4f);
    dtc->envSlowMid.setSampleRate(fs);
    dtc->envSlowMid.setParameters(0.005f, 0.08f, 1.0f, 0.08f);
    dtc->envSlowMid.setShape(0.2f);

    // Per-band transient followers – Hi (faster ballistics)
    dtc->envFastHi.setSampleRate(fs);
    dtc->envFastHi.setParameters(0.00005f, 0.003f, 1.0f, 0.003f);
    dtc->envFastHi.setShape(-0.4f);
    dtc->envSlowHi.setSampleRate(fs);
    dtc->envSlowHi.setParameters(0.003f, 0.05f, 1.0f, 0.05f);
    dtc->envSlowHi.setShape(0.2f);

    seed_xorshift(1337);

    // Input HP: coefficient for one-pole LP used to derive high-pass
    dtc->inputHPCoeff = expf(-2.0f * 3.14159265f * dtc->inputHPFreq / fs);
    // Output LP: coefficient pre-computed; step() skips when freq == 20000 Hz
    dtc->outputLPCoeff = expf(-2.0f * 3.14159265f * dtc->outputLPFreq / fs);

    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;
    return alg;
}

static inline float bitCrushWithDither(float x, int bits) {
    if (bits >= 16) return x;
    if (bits < 1) bits = 1;
    const float levels = (float)(1 << bits);
    const float dither = (((float)xorshift16() / 65535.0f) - 0.5f) / levels;
    return std::round((x + dither) * levels) / levels;
}

static inline float shapedTransientGain(float transient, float attack, float sustain) {
    float gain = 1.0f + (transient * attack) + ((-transient) * sustain);
    return clampf(gain, 0.0f, 4.0f);
}

static void parameterChanged(_NT_algorithm* self, int p) {
    auto* a = static_cast<_NerberusAlgorithm*>(self);
    auto* dtc = a->dtc;

    switch (p) {
        case kParamCrossLo:
            dtc->crossFreqLo = (float)a->v[p];
            dtc->splitter.updateCoeffs(dtc->crossFreqLo, dtc->crossFreqHi, dtc->sampleRate);
            break;
        case kParamCrossHi:
            dtc->crossFreqHi = (float)a->v[p];
            dtc->splitter.updateCoeffs(dtc->crossFreqLo, dtc->crossFreqHi, dtc->sampleRate);
            break;
        case kParamDrive:    { float t = a->v[p] * 0.001f; dtc->drive = t * sqrtf(t); break; }
        case kParamPressLo:   dtc->pressLo  = a->v[p] * 0.001f; break;
        case kParamPressMid:  dtc->pressMid = a->v[p] * 0.001f; break;
        case kParamPressHi:   dtc->pressHi  = a->v[p] * 0.001f; break;
        case kParamGritLo:   dtc->gritLo     = a->v[p] * 0.001f; break;
        case kParamGritMid:  dtc->gritMid    = a->v[p] * 0.001f; break;
        case kParamGritHi:   dtc->gritHi     = a->v[p] * 0.001f; break;
        case kParamMix:      dtc->mix        = a->v[p] * 0.001f; break;
        case kParamOutput:   dtc->outputGain = a->v[p] * 0.001f; break;
        case kParamFilterModel: {
            FilterModel m = static_cast<FilterModel>(a->v[p]);
            dtc->filterL.setModel(m);
            dtc->filterR.setModel(m);
            dtc->filterL2x.setModel(m);
            dtc->filterR2x.setModel(m);
            dtc->filterL4x.setModel(m);
            dtc->filterR4x.setModel(m);
            break;
        }
        case kParamFilterCutoff: {
            const float norm = a->v[p] / 10000.0f;
            dtc->filterCutoff = 20.0f * powf(1000.0f, norm);
            break;
        }
        case kParamFilterRes:    dtc->filterResonance = a->v[p] * 0.000999f; break;
        case kParamFilterDrive:  dtc->filterDrive     = a->v[p] / 1000.0f; break;
        case kParamFilterCutoffEnv: dtc->filterCutoffEnv = a->v[p] * 0.001f; break;
        case kParamFilterDriveEnv:  dtc->filterDriveEnv  = a->v[p] * 0.001f; break;
        case kParamFilterResEnv:    dtc->filterResEnv    = a->v[p] * 0.001f; break;
        case kParamFilterOversample: dtc->filterOversample = a->v[p]; break;
        case kParamBitDepthLo:    dtc->bitDepthLo  = a->v[p]; break;
        case kParamBitDepthMid:   dtc->bitDepthMid = a->v[p]; break;
        case kParamBitDepthHi:    dtc->bitDepthHi  = a->v[p]; break;
        case kParamDecimationLo:  dtc->decimLo  = (a->v[p] < 1) ? 1 : a->v[p]; break;
        case kParamDecimationMid: dtc->decimMid = (a->v[p] < 1) ? 1 : a->v[p]; break;
        case kParamDecimationHi:  dtc->decimHi  = (a->v[p] < 1) ? 1 : a->v[p]; break;
        case kParamNoiseLevelLo:  dtc->noiseLevelLo  = a->v[p] * 0.001f; break;
        case kParamNoiseLevelMid: dtc->noiseLevelMid = a->v[p] * 0.001f; break;
        case kParamNoiseLevelHi:  dtc->noiseLevelHi  = a->v[p] * 0.001f; break;
        case kParamNoiseColor:    dtc->noiseColor    = a->v[p]; break;
        case kParamRingFreq:
            dtc->ringFreqHz = (float)a->v[p];
            dtc->ringCarrier.setFrequency(dtc->ringFreqHz);
            break;
        case kParamCVFilterFreqDepth: dtc->cvFilterFreqDepth = a->v[p] * 0.001f; break;
        case kParamCVRingFreqDepth:   dtc->cvRingFreqDepth   = a->v[p] * 0.001f; break;
        case kParamInputHP: {
            dtc->inputHPFreq  = (float)a->v[p];
            dtc->inputHPCoeff = expf(-2.0f * 3.14159265f * dtc->inputHPFreq / dtc->sampleRate);
            break;
        }
        case kParamBias: dtc->biasAmount = a->v[p] * 0.001f; break;
        case kParamRingDepthLo:   dtc->ringDepthLo  = a->v[p] * 0.001f; break;
        case kParamRingDepthMid:  dtc->ringDepthMid = a->v[p] * 0.001f; break;
        case kParamRingDepthHi:   dtc->ringDepthHi  = a->v[p] * 0.001f; break;
        case kParamWidthLo:       dtc->widthLo  = a->v[p] * 0.001f; break;
        case kParamWidthMid:      dtc->widthMid = a->v[p] * 0.001f; break;
        case kParamWidthHi:       dtc->widthHi  = a->v[p] * 0.001f; break;
        case kParamEnvSensitivity: dtc->envSensitivity = a->v[p] * 0.001f; break;
        case kParamEnvAttack: {
            dtc->envAttackMs = (float)a->v[p];
            dtc->envFollower.setTimes(dtc->envAttackMs * 0.001f, dtc->envReleaseMs * 0.001f);
            break;
        }
        case kParamEnvRelease: {
            dtc->envReleaseMs = (float)a->v[p];
            dtc->envFollower.setTimes(dtc->envAttackMs * 0.001f, dtc->envReleaseMs * 0.001f);
            break;
        }
        case kParamEnvShape: {
            dtc->envShape = a->v[p] * 0.001f;
            break;
        }
        case kParamTransientAttackLo:   dtc->transientAttackLo   = a->v[p] * 0.001f; break;
        case kParamTransientAttackMid:  dtc->transientAttackMid  = a->v[p] * 0.001f; break;
        case kParamTransientAttackHi:   dtc->transientAttackHi   = a->v[p] * 0.001f; break;
        case kParamTransientSustainLo:  dtc->transientSustainLo  = a->v[p] * 0.001f; break;
        case kParamTransientSustainMid: dtc->transientSustainMid = a->v[p] * 0.001f; break;
        case kParamTransientSustainHi:  dtc->transientSustainHi  = a->v[p] * 0.001f; break;
        case kParamOutputLP: {
            dtc->outputLPFreq  = (float)a->v[p];
            dtc->outputLPCoeff = expf(-2.0f * 3.14159265f * dtc->outputLPFreq / dtc->sampleRate);
            break;
        }
        case kParamOutLimiter: dtc->outLimiter = a->v[p] * 0.001f; break;
        case kParamLoSatMode:  dtc->loSatMode  = a->v[p]; break;
        default: break;
    }
}

static void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    auto* a = static_cast<_NerberusAlgorithm*>(self);
    auto* dtc = a->dtc;

    const int numFrames = numFramesBy4 * 4;

    const float* inL = busPtr(busFrames, numFrames, a->v[kParamInL]);
    const float* inR = busPtr(busFrames, numFrames, a->v[kParamInR]);
    float* outL = busPtr(busFrames, numFrames, a->v[kParamOutL]);
    float* outR = busPtr(busFrames, numFrames, a->v[kParamOutR]);

    if (!outL && !outR) return;

    const float* cvFiltBus = busPtr(busFrames, numFrames, a->v[kParamCVFilterFreqIn]);
    const float* cvRingBus = busPtr(busFrames, numFrames, a->v[kParamCVRingFreqIn]);

    float dryBufL[kBlockChunk], dryBufR[kBlockChunk];
    float loL[kBlockChunk],     loR[kBlockChunk];
    float midL[kBlockChunk],    midR[kBlockChunk];
    float hiL[kBlockChunk],     hiR[kBlockChunk];
    float wetBufL[kBlockChunk], wetBufR[kBlockChunk];

    int offset = 0;
    while (offset < numFrames) {
        const int n = (numFrames - offset > kBlockChunk) ? kBlockChunk : (numFrames - offset);

        float blockPeakLo  = 0.f, blockPeakMid = 0.f, blockPeakHi = 0.f;

        // Block-average CV inputs
        float cvFilt = 0.f;
        if (cvFiltBus) {
            for (int i = 0; i < n; ++i) cvFilt += cvFiltBus[offset + i];
            cvFilt *= (1.0f / (float)n);
        }
        float cvRingVal = 0.f;
        if (cvRingBus) {
            for (int i = 0; i < n; ++i) cvRingVal += cvRingBus[offset + i];
            cvRingVal *= (1.0f / (float)n);
        }

        // ---- 1. Split input into bands; apply per-band crush/decimation ----
        for (int i = 0; i < n; ++i) {
            const float sL = inL ? inL[offset + i] : 0.f;
            const float sR = inR ? inR[offset + i] : 0.f;

            dryBufL[i] = sL;  // dry path always uses raw input
            dryBufR[i] = sR;

            const float mag = std::fmax(std::fabs(sL), std::fabs(sR));
            dtc->envFollower.process(mag);  // per-sample: exact timing regardless of block size

            // Input high-pass conditioning: one-pole LP used to derive HP
            // inputHPCoeff ≈ 0.9974 at default 20 Hz → transparent
            // inputHPCoeff ≈ 0.9741 at 200 Hz → removes guitar low-end mud before saturation
            dtc->hpStateL = dtc->inputHPCoeff * dtc->hpStateL + (1.0f - dtc->inputHPCoeff) * sL;
            dtc->hpStateR = dtc->inputHPCoeff * dtc->hpStateR + (1.0f - dtc->inputHPCoeff) * sR;
            const float hpL = sL - dtc->hpStateL;
            const float hpR = sR - dtc->hpStateR;

            dtc->splitter.split(hpL, hpR,
                                loL[i], midL[i], hiL[i],
                                loR[i], midR[i], hiR[i]);

            // Low band crush
            if (++dtc->decimPhaseLo >= dtc->decimLo) {
                dtc->decimPhaseLo  = 0;
                dtc->decimHoldLoL  = bitCrushWithDither(loL[i],  dtc->bitDepthLo);
                dtc->decimHoldLoR  = bitCrushWithDither(loR[i],  dtc->bitDepthLo);
            }
            loL[i] = dtc->decimHoldLoL;
            loR[i] = dtc->decimHoldLoR;

            // Mid band crush
            if (++dtc->decimPhaseMid >= dtc->decimMid) {
                dtc->decimPhaseMid = 0;
                dtc->decimHoldMidL = bitCrushWithDither(midL[i], dtc->bitDepthMid);
                dtc->decimHoldMidR = bitCrushWithDither(midR[i], dtc->bitDepthMid);
            }
            midL[i] = dtc->decimHoldMidL;
            midR[i] = dtc->decimHoldMidR;

            // Hi band crush
            if (++dtc->decimPhaseHi >= dtc->decimHi) {
                dtc->decimPhaseHi  = 0;
                dtc->decimHoldHiL  = bitCrushWithDither(hiL[i],  dtc->bitDepthHi);
                dtc->decimHoldHiR  = bitCrushWithDither(hiR[i],  dtc->bitDepthHi);
            }
            hiL[i] = dtc->decimHoldHiL;
            hiR[i] = dtc->decimHoldHiR;

            const float mLo  = std::fmax(std::fabs(loL[i]),  std::fabs(loR[i]));
            const float mMid = std::fmax(std::fabs(midL[i]), std::fabs(midR[i]));
            const float mHi  = std::fmax(std::fabs(hiL[i]),  std::fabs(hiR[i]));
            if (mLo  > blockPeakLo)  blockPeakLo  = mLo;
            if (mMid > blockPeakMid) blockPeakMid = mMid;
            if (mHi  > blockPeakHi)  blockPeakHi  = mHi;
        }

        // ---- 2. Envelope-driven overall drive ----
        // PeakFollower updated per-sample above; read the end-of-block level here.
        const float envDriveRaw = dtc->envFollower.getLevel();
        // Apply shape curve: f(x) = x*(1+s)/(1+s*x)
        const float envDrive = (std::fabs(dtc->envShape) < 0.001f) ? envDriveRaw
            : envDriveRaw * (1.0f + dtc->envShape) / (1.0f + dtc->envShape * envDriveRaw);
        const float effectiveDrive = clampf(
            dtc->drive + (envDrive * dtc->envSensitivity * (1.0f - dtc->drive)),
            0.0f, 1.0f);

        // ---- 3. Engine: per-band dynamics + saturation ----
        // Dynamic bias: apply DC offset to lo + mid before saturation so the
        // nonlinearity generates even-order harmonics (asymmetric clipping).
        // The offset is proportional to envDrive, so heavy transients shift the
        // bias the most; sustained notes decay back toward symmetric saturation.
        // Bias is removed after the engine — harmonic asymmetry is retained.
        const float blockBias = envDrive * dtc->biasAmount * 0.08f;
        if (blockBias > 0.0001f) {
            for (int i = 0; i < n; ++i) {
                loL[i]  += blockBias;  loR[i]  += blockBias;
                midL[i] += blockBias;  midR[i] += blockBias;
            }
        }

        dtc->engine.processBandsInPlace(
            loL, loR, midL, midR, hiL, hiR, n,
            effectiveDrive, dtc->pressLo, dtc->pressMid, dtc->pressHi,
            dtc->gritLo, dtc->gritMid, dtc->gritHi,
            dtc->loSatMode != 0);

        if (blockBias > 0.0001f) {
            for (int i = 0; i < n; ++i) {
                loL[i]  -= blockBias;  loR[i]  -= blockBias;
                midL[i] -= blockBias;  midR[i] -= blockBias;
            }
        }

        // ---- 4. Per-band envelope followers for transient detection ----
        const bool gateLo  = blockPeakLo  > 0.01f;
        const bool gateMid = blockPeakMid > 0.01f;
        const bool gateHi  = blockPeakHi  > 0.01f;

        dtc->envFastLo.gate(gateLo);  dtc->envSlowLo.gate(gateLo);
        dtc->envFastLo.advanceBlock(n); dtc->envSlowLo.advanceBlock(n);
        const float fLoS = dtc->envFastLo.getCurrentLevelShaped();
        const float fLoE = dtc->envFastLo.getTargetLevelShaped();
        const float sLoS = dtc->envSlowLo.getCurrentLevelShaped();
        const float sLoE = dtc->envSlowLo.getTargetLevelShaped();
        dtc->envFastLo.finalizeBlock(); dtc->envSlowLo.finalizeBlock();

        dtc->envFastMid.gate(gateMid); dtc->envSlowMid.gate(gateMid);
        dtc->envFastMid.advanceBlock(n); dtc->envSlowMid.advanceBlock(n);
        const float fMidS = dtc->envFastMid.getCurrentLevelShaped();
        const float fMidE = dtc->envFastMid.getTargetLevelShaped();
        const float sMidS = dtc->envSlowMid.getCurrentLevelShaped();
        const float sMidE = dtc->envSlowMid.getTargetLevelShaped();
        dtc->envFastMid.finalizeBlock(); dtc->envSlowMid.finalizeBlock();

        dtc->envFastHi.gate(gateHi);  dtc->envSlowHi.gate(gateHi);
        dtc->envFastHi.advanceBlock(n); dtc->envSlowHi.advanceBlock(n);
        const float fHiS = dtc->envFastHi.getCurrentLevelShaped();
        const float fHiE = dtc->envFastHi.getTargetLevelShaped();
        const float sHiS = dtc->envSlowHi.getCurrentLevelShaped();
        const float sHiE = dtc->envSlowHi.getTargetLevelShaped();
        dtc->envFastHi.finalizeBlock(); dtc->envSlowHi.finalizeBlock();

        const bool anyNoise = (dtc->noiseLevelLo + dtc->noiseLevelMid + dtc->noiseLevelHi) > 0.0001f;

        // Apply CV to ring carrier frequency for this block
        if (cvRingBus) {
            dtc->ringCarrier.setFrequency(
                clampf(dtc->ringFreqHz * powf(2.0f, cvRingVal * dtc->cvRingFreqDepth),
                       20.0f, 10000.0f));
        }

        // Pre-fill ring carrier buffer (in DTC — avoids stack growth); hoist all
        // loop-invariant conditions out of the hot loop.
        // Carrier: avoids per-sample sine LFO overhead inside the critical path.
        // Booleans: prevents repeated comparisons and allows the compiler to eliminate dead branches.
        // tStep:    replaces per-sample float division with a cheap accumulation.
        float* carrierBuf = dtc->ringCarrierBuf;
        for (int i = 0; i < n; ++i) carrierBuf[i] = dtc->ringCarrier.getNextValue();
        const bool hasRingLo   = dtc->ringDepthLo  > 0.0001f;
        const bool hasRingMid  = dtc->ringDepthMid > 0.0001f;
        const bool hasRingHi   = dtc->ringDepthHi  > 0.0001f;
        const bool hasWidthLo  = dtc->widthLo  != 1.f;
        const bool hasWidthMid = dtc->widthMid != 1.f;
        const bool hasWidthHi  = dtc->widthHi  != 1.f;
        const float tStep = (n > 1) ? (1.0f / (float)(n - 1)) : 0.f;

        // ---- 5. Per-sample: ring mod, transient, noise, width, recombine ----
        for (int i = 0; i < n; ++i) {
            const float t       = tStep * (float)i;
            const float carrier = carrierBuf[i];

            // Generate noise once per sample; scale per-band by noiseLevel
            float noiseVal = 0.f;
            if (anyNoise) {
                float white = ((float)xorshift16() / 32767.5f) - 1.0f;
                noiseVal = white;
                if (dtc->noiseColor >= 1) {
                    dtc->pink0 = 0.99886f * dtc->pink0 + white * 0.0555179f;
                    dtc->pink1 = 0.99332f * dtc->pink1 + white * 0.0750759f;
                    dtc->pink2 = 0.96900f * dtc->pink2 + white * 0.1538520f;
                    const float pink = (dtc->pink0 + dtc->pink1 + dtc->pink2 + white * 0.5362f) * 0.11f;
                    noiseVal = pink;
                    if (dtc->noiseColor >= 2) {
                        dtc->lofiState += 0.0615f * (pink - dtc->lofiState);
                        noiseVal = dtc->lofiState;
                    }
                }
            }

            // --- Low band ---
            if (hasRingLo) {
                loL[i] = loL[i] * (1.f - dtc->ringDepthLo) + loL[i] * carrier * dtc->ringDepthLo;
                loR[i] = loR[i] * (1.f - dtc->ringDepthLo) + loR[i] * carrier * dtc->ringDepthLo;
            }
            {
                const float fast = fLoS + (fLoE - fLoS) * t;
                const float slow = sLoS + (sLoE - sLoS) * t;
                const float g = shapedTransientGain(fast - slow,
                                                    dtc->transientAttackLo, dtc->transientSustainLo);
                loL[i] *= g; loR[i] *= g;
            }
            loL[i] += noiseVal * dtc->noiseLevelLo;
            loR[i] += noiseVal * dtc->noiseLevelLo;
            if (hasWidthLo) {
                const float m = 0.5f * (loL[i] + loR[i]);
                const float s = 0.5f * (loL[i] - loR[i]) * dtc->widthLo;
                loL[i] = m + s; loR[i] = m - s;
            }

            // --- Mid band ---
            if (hasRingMid) {
                midL[i] = midL[i] * (1.f - dtc->ringDepthMid) + midL[i] * carrier * dtc->ringDepthMid;
                midR[i] = midR[i] * (1.f - dtc->ringDepthMid) + midR[i] * carrier * dtc->ringDepthMid;
            }
            {
                const float fast = fMidS + (fMidE - fMidS) * t;
                const float slow = sMidS + (sMidE - sMidS) * t;
                const float g = shapedTransientGain(fast - slow,
                                                    dtc->transientAttackMid, dtc->transientSustainMid);
                midL[i] *= g; midR[i] *= g;
            }
            midL[i] += noiseVal * dtc->noiseLevelMid;
            midR[i] += noiseVal * dtc->noiseLevelMid;
            if (hasWidthMid) {
                const float m = 0.5f * (midL[i] + midR[i]);
                const float s = 0.5f * (midL[i] - midR[i]) * dtc->widthMid;
                midL[i] = m + s; midR[i] = m - s;
            }

            // --- Hi band ---
            if (hasRingHi) {
                hiL[i] = hiL[i] * (1.f - dtc->ringDepthHi) + hiL[i] * carrier * dtc->ringDepthHi;
                hiR[i] = hiR[i] * (1.f - dtc->ringDepthHi) + hiR[i] * carrier * dtc->ringDepthHi;
            }
            {
                const float fast = fHiS + (fHiE - fHiS) * t;
                const float slow = sHiS + (sHiE - sHiS) * t;
                const float g = shapedTransientGain(fast - slow,
                                                    dtc->transientAttackHi, dtc->transientSustainHi);
                hiL[i] *= g; hiR[i] *= g;
            }
            hiL[i] += noiseVal * dtc->noiseLevelHi;
            hiR[i] += noiseVal * dtc->noiseLevelHi;
            if (hasWidthHi) {
                const float m = 0.5f * (hiL[i] + hiR[i]);
                const float s = 0.5f * (hiL[i] - hiR[i]) * dtc->widthHi;
                hiL[i] = m + s; hiR[i] = m - s;
            }

            // Recombine wet bands
            wetBufL[i] = loL[i] + midL[i] + hiL[i];
            wetBufR[i] = loR[i] + midR[i] + hiR[i];
        }

        // ---- 6. Filter on recombined wet signal (full-band) ----
        // Apply envelope follower amounts + CV modulation to filter parameters
        const float fltCutoffEnvMod = clampf(
            dtc->filterCutoff * powf(2.0f, envDrive * dtc->filterCutoffEnv * 4.0f),
            20.0f, 20000.0f);
        const float fltCutoff = cvFiltBus
            ? clampf(fltCutoffEnvMod * powf(2.0f, cvFilt * dtc->cvFilterFreqDepth), 20.0f, 20000.0f)
            : fltCutoffEnvMod;
        const float fltDrive = clampf(
            dtc->filterDrive + envDrive * dtc->filterDriveEnv * 9.0f,
            1.0f, 10.0f);
        const float fltRes = clampf(
            dtc->filterResonance + envDrive * dtc->filterResEnv,
            0.0f, 0.999f);

        const FilterMode filterMode = static_cast<FilterMode>(a->v[kParamFilterMode]);
        if (filterMode != FilterMode::BYPASS) {
            if (dtc->filterOversample <= 0) {
                dtc->filterL.processBlock(wetBufL, wetBufL, n,
                                          fltCutoff, fltRes,
                                          fltDrive, filterMode);
                dtc->filterR.processBlock(wetBufR, wetBufR, n,
                                          fltCutoff, fltRes,
                                          fltDrive, filterMode);
            } else if (dtc->filterOversample == 1) {
                // Block-level 2x oversampling: upsample entire block, one tan() for the full
                // upsampled block, then downsample — avoids 63 redundant coefficient setups.
                // Buffers live in DTC (not stack) to avoid stack overflow in the audio thread.
                float* up2L = dtc->filterOsBufL;
                float* up2R = dtc->filterOsBufR;
                for (int i = 0; i < n; ++i) {
                    dtc->filterOS2xL.upsample(wetBufL[i], up2L[2*i], up2L[2*i+1]);
                    dtc->filterOS2xR.upsample(wetBufR[i], up2R[2*i], up2R[2*i+1]);
                }
                dtc->filterL2x.processBlock(up2L, up2L, n * 2,
                                            fltCutoff, fltRes, fltDrive, filterMode);
                dtc->filterR2x.processBlock(up2R, up2R, n * 2,
                                            fltCutoff, fltRes, fltDrive, filterMode);
                for (int i = 0; i < n; ++i)
                    wetBufL[i] = dtc->filterOS2xL.downsample(up2L[2*i], up2L[2*i+1]);
                for (int i = 0; i < n; ++i)
                    wetBufR[i] = dtc->filterOS2xR.downsample(up2R[2*i], up2R[2*i+1]);
            } else {
                // Block-level 4x oversampling
                float* up4L = dtc->filterOsBufL;
                float* up4R = dtc->filterOsBufR;
                for (int i = 0; i < n; ++i) {
                    dtc->filterOS4xL.upsample4x(wetBufL[i], &up4L[4*i]);
                    dtc->filterOS4xR.upsample4x(wetBufR[i], &up4R[4*i]);
                }
                dtc->filterL4x.processBlock(up4L, up4L, n * 4,
                                            fltCutoff, fltRes, fltDrive, filterMode);
                dtc->filterR4x.processBlock(up4R, up4R, n * 4,
                                            fltCutoff, fltRes, fltDrive, filterMode);
                for (int i = 0; i < n; ++i)
                    wetBufL[i] = dtc->filterOS4xL.downsample4x(&up4L[4*i]);
                for (int i = 0; i < n; ++i)
                    wetBufR[i] = dtc->filterOS4xR.downsample4x(&up4R[4*i]);
            }
        }

        // ---- 6b. Output LP: one-pole low-pass on recombined wet signal ----
        // Default freq 20000 Hz = transparent (skipped). Reduce to ~6-12 kHz for
        // cab/speaker rolloff — tames fizzy high-frequency saturation artefacts.
        if (dtc->outputLPFreq < 20000.0f) {
            for (int i = 0; i < n; ++i) {
                dtc->lpOutStateL = dtc->outputLPCoeff * dtc->lpOutStateL
                                 + (1.0f - dtc->outputLPCoeff) * wetBufL[i];
                dtc->lpOutStateR = dtc->outputLPCoeff * dtc->lpOutStateR
                                 + (1.0f - dtc->outputLPCoeff) * wetBufR[i];
                wetBufL[i] = dtc->lpOutStateL;
                wetBufR[i] = dtc->lpOutStateR;
            }
        }

        // ---- 7. Mix dry/wet and write output ----
        // Precompute combined gains and hoist all loop-invariant conditions to eliminate
        // per-sample branches and redundant multiplies.
        {
            const float dryGain    = (1.0f - dtc->mix) * dtc->outputGain;
            const float wetGain    = dtc->mix * dtc->outputGain;
            const bool  hasLimiter = dtc->outLimiter > 0.001f;
            const bool  outLMix    = outL && (a->v[kParamOutLMode] == 0);
            const bool  outRMix    = outR && (a->v[kParamOutRMode] == 0);
            for (int i = 0; i < n; ++i) {
                float outSigL = dryBufL[i] * dryGain + wetBufL[i] * wetGain;
                float outSigR = dryBufR[i] * dryGain + wetBufR[i] * wetGain;

                // Soft output limiter: blends linear output with cheap_saturate-clipped version.
                // At outLimiter=0: bypass. At outLimiter=1: peaks above ~±1 are soft-clipped.
                if (hasLimiter) {
                    outSigL += (cheap_saturate(outSigL) - outSigL) * dtc->outLimiter;
                    outSigR += (cheap_saturate(outSigR) - outSigR) * dtc->outLimiter;
                }

                if (outL) { if (outLMix) outL[offset + i] += outSigL; else outL[offset + i] = outSigL; }
                if (outR) { if (outRMix) outR[offset + i] += outSigR; else outR[offset + i] = outSigR; }
            }
        }

        offset += n;
    }
}


static const _NT_factory kFactory = {
    .guid = NT_MULTICHAR('C', 'R', 'B', 'R'),
    .name = "Nerberus",
    .description = "Triple-band compressed grit and drive",
    .numSpecifications = 0,
    .specifications = nullptr,
    .calculateStaticRequirements = nullptr,
    .initialise = nullptr,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = parameterChanged,
    .step = step,
    .draw = nullptr,
    .midiRealtime = nullptr,
    .midiMessage = nullptr,
    .tags = kNT_tagEffect,
    .hasCustomUi = nullptr,
    .customUi = nullptr,
    .setupUi = nullptr,
    .serialise = nullptr,
    .deserialise = nullptr,
    .midiSysEx = nullptr,
    .parameterUiPrefix = nullptr,
    .parameterString = nullptr,
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    if (selector == kNT_selector_version) return kNT_apiVersionCurrent;
    if (selector == kNT_selector_numFactories) return 1;
    if (selector == kNT_selector_factoryInfo && data == 0) return (uintptr_t)&kFactory;
    return 0;
}
