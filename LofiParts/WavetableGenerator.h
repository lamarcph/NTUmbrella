// =============================================================================
// WavetableGenerator.h — Reusable wavetable generation for disting NT
// =============================================================================
// Header-only, no NT API dependencies. Use inside plugins, tests, or
// standalone tools.
//
// Buffer layout: numWaves contiguous single-cycle waves, each waveLength
// samples (int16_t).  Total buffer size = numWaves * waveLength.
//
//   wave0[waveLength] | wave1[waveLength] | ... | waveN-1[waveLength]
//
// The oscillator morphs across waves using a morph parameter (0..32767).
// =============================================================================

#ifndef LOFI_PARTS_WAVETABLE_GENERATOR_H
#define LOFI_PARTS_WAVETABLE_GENERATOR_H

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace WtGen {

static constexpr float PI  = 3.14159265358979323846f;
static constexpr float TWO_PI = 2.0f * PI;

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

inline int16_t floatToQ15(float v) {
    float c = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
    return static_cast<int16_t>(c * 32767.0f);
}

/// Normalize buffer so peak amplitude maps to full-scale Q15.
inline void normalize(int16_t* buf, uint32_t totalSamples) {
    int32_t peak = 0;
    for (uint32_t i = 0; i < totalSamples; ++i) {
        int32_t a = buf[i] < 0 ? -static_cast<int32_t>(buf[i]) : buf[i];
        if (a > peak) peak = a;
    }
    if (peak == 0) return;
    float scale = 32767.0f / static_cast<float>(peak);
    for (uint32_t i = 0; i < totalSamples; ++i) {
        float v = static_cast<float>(buf[i]) * scale;
        buf[i] = static_cast<int16_t>(
            v < -32767.0f ? -32767.0f : (v > 32767.0f ? 32767.0f : v));
    }
}

/// Remove DC offset from a single wave.
inline void dcBlock(int16_t* buf, uint32_t waveLength) {
    int32_t sum = 0;
    for (uint32_t i = 0; i < waveLength; ++i) sum += buf[i];
    int16_t dc = static_cast<int16_t>(sum / static_cast<int32_t>(waveLength));
    for (uint32_t i = 0; i < waveLength; ++i) {
        int32_t v = static_cast<int32_t>(buf[i]) - dc;
        buf[i] = static_cast<int16_t>(
            v < -32767 ? -32767 : (v > 32767 ? 32767 : v));
    }
}

/// DC-block every wave in a multi-wave buffer.
inline void dcBlockAll(int16_t* buf, uint32_t numWaves, uint32_t waveLength) {
    for (uint32_t w = 0; w < numWaves; ++w)
        dcBlock(buf + w * waveLength, waveLength);
}

// ---------------------------------------------------------------------------
// Wave shape functions:  phase ∈ [0, 1) → sample ∈ [-1, 1]
// ---------------------------------------------------------------------------

typedef float (*ShapeFn)(float phase);

inline float shapeSine(float p)     { return sinf(p * TWO_PI); }
inline float shapeSaw(float p)      { return 2.0f * p - 1.0f; }
inline float shapeSquare(float p)   { return p < 0.5f ? 1.0f : -1.0f; }
inline float shapeTriangle(float p) {
    return p < 0.5f ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
}

// ---------------------------------------------------------------------------
// Single-wave fill
// ---------------------------------------------------------------------------

/// Fill one wave (waveLength samples) from a shape function.
inline void fillWave(int16_t* buf, uint32_t waveLength, ShapeFn fn) {
    for (uint32_t s = 0; s < waveLength; ++s)
        buf[s] = floatToQ15(fn(static_cast<float>(s) / waveLength));
}

// ---------------------------------------------------------------------------
// Multi-wave generators — fill numWaves × waveLength samples
// ---------------------------------------------------------------------------

/// Linear crossfade between two shape functions across waves.
inline void morphShapes(int16_t* buf, uint32_t numWaves, uint32_t waveLen,
                        ShapeFn a, ShapeFn b) {
    for (uint32_t w = 0; w < numWaves; ++w) {
        float blend = numWaves > 1
            ? static_cast<float>(w) / (numWaves - 1) : 0.0f;
        int16_t* dst = buf + w * waveLen;
        for (uint32_t s = 0; s < waveLen; ++s) {
            float p = static_cast<float>(s) / waveLen;
            float va = a(p), vb = b(p);
            dst[s] = floatToQ15(va + (vb - va) * blend);
        }
    }
}

/// Additive harmonics: wave 0 = fundamental only → wave N-1 = maxHarmonics.
/// rolloff controls amplitude decay: amp[h] = 1 / h^rolloff.
/// (rolloff=1 gives sawtooth-like spectrum, rolloff=2 closer to triangle.)
inline void additive(int16_t* buf, uint32_t numWaves, uint32_t waveLen,
                     uint32_t maxHarmonics = 32, float rolloff = 1.0f) {
    for (uint32_t w = 0; w < numWaves; ++w) {
        float numH = 1.0f + static_cast<float>(w) / (numWaves - 1)
                          * (maxHarmonics - 1);
        int16_t* dst = buf + w * waveLen;
        for (uint32_t s = 0; s < waveLen; ++s) {
            float p = static_cast<float>(s) / waveLen;
            float v = 0.0f;
            for (uint32_t h = 1; h <= static_cast<uint32_t>(numH); ++h) {
                float amp = 1.0f / powf(static_cast<float>(h), rolloff);
                if (h == static_cast<uint32_t>(numH) && numH != floorf(numH))
                    amp *= (numH - floorf(numH));
                v += amp * sinf(p * TWO_PI * h);
            }
            dst[s] = floatToQ15(v);  // may clip before normalize
        }
    }
    normalize(buf, numWaves * waveLen);
}

/// PWM: duty cycle sweeps from ~5 % to ~95 % across waves.
inline void pwm(int16_t* buf, uint32_t numWaves, uint32_t waveLen) {
    for (uint32_t w = 0; w < numWaves; ++w) {
        float duty = 0.05f + 0.9f * static_cast<float>(w) / (numWaves - 1);
        int16_t* dst = buf + w * waveLen;
        for (uint32_t s = 0; s < waveLen; ++s) {
            float p = static_cast<float>(s) / waveLen;
            dst[s] = floatToQ15(p < duty ? 1.0f : -1.0f);
        }
    }
}

/// FM synthesis: sine carrier + sine modulator with increasing mod index.
inline void fm(int16_t* buf, uint32_t numWaves, uint32_t waveLen,
               float ratio = 2.0f, float maxIndex = 8.0f) {
    for (uint32_t w = 0; w < numWaves; ++w) {
        float idx = maxIndex * static_cast<float>(w) / (numWaves - 1);
        int16_t* dst = buf + w * waveLen;
        for (uint32_t s = 0; s < waveLen; ++s) {
            float p = static_cast<float>(s) / waveLen;
            float mod = sinf(p * TWO_PI * ratio);
            dst[s] = floatToQ15(sinf(p * TWO_PI + idx * mod));
        }
    }
    normalize(buf, numWaves * waveLen);
}

/// Wavefolded sine: increasing gain → triangle fold.
inline void wavefold(int16_t* buf, uint32_t numWaves, uint32_t waveLen,
                     float maxGain = 8.0f) {
    for (uint32_t w = 0; w < numWaves; ++w) {
        float gain = 1.0f + (maxGain - 1.0f)
                          * static_cast<float>(w) / (numWaves - 1);
        int16_t* dst = buf + w * waveLen;
        for (uint32_t s = 0; s < waveLen; ++s) {
            float p = static_cast<float>(s) / waveLen;
            float v = sinf(p * TWO_PI) * gain;
            // Triangle fold
            while (v > 1.0f || v < -1.0f) {
                if (v > 1.0f)  v =  2.0f - v;
                if (v < -1.0f) v = -2.0f - v;
            }
            dst[s] = floatToQ15(v);
        }
    }
}

/// Formant-like: Gaussian-windowed sine burst, formant ratio sweeps across
/// waves from minRatio to maxRatio (relative to fundamental).
inline void formant(int16_t* buf, uint32_t numWaves, uint32_t waveLen,
                    float minRatio = 1.0f, float maxRatio = 16.0f) {
    for (uint32_t w = 0; w < numWaves; ++w) {
        float r = minRatio + (maxRatio - minRatio)
                            * static_cast<float>(w) / (numWaves - 1);
        int16_t* dst = buf + w * waveLen;
        for (uint32_t s = 0; s < waveLen; ++s) {
            float p = static_cast<float>(s) / waveLen;
            float window = expf(-0.5f * powf((p - 0.5f) * 4.0f, 2.0f));
            dst[s] = floatToQ15(window * sinf(p * TWO_PI * r));
        }
    }
    normalize(buf, numWaves * waveLen);
}

/// Supersaw: increasing detuned saw layers across waves.
/// Wave 0 = single saw, wave N-1 = numLayers saws spread ± maxDetuneCents.
inline void supersaw(int16_t* buf, uint32_t numWaves, uint32_t waveLen,
                     uint32_t numLayers = 7, float maxDetuneCents = 40.0f) {
    for (uint32_t w = 0; w < numWaves; ++w) {
        float detuneRange = maxDetuneCents
            * static_cast<float>(w) / (numWaves - 1);
        int16_t* dst = buf + w * waveLen;
        for (uint32_t s = 0; s < waveLen; ++s) {
            float p = static_cast<float>(s) / waveLen;
            float v = 0.0f;
            for (uint32_t l = 0; l < numLayers; ++l) {
                float cents = numLayers > 1
                    ? -detuneRange + 2.0f * detuneRange * l / (numLayers - 1)
                    : 0.0f;
                float ratio = powf(2.0f, cents / 1200.0f);
                float pp = fmodf(p * ratio, 1.0f);
                v += 2.0f * pp - 1.0f;
            }
            v /= numLayers;
            dst[s] = floatToQ15(v);
        }
    }
}

// ---------------------------------------------------------------------------
// Custom generator callback
// ---------------------------------------------------------------------------

/// (wave, sample, numWaves, waveLen) → float in [-1, 1]
typedef float (*CustomGenFn)(uint32_t wave, uint32_t sample,
                             uint32_t numWaves, uint32_t waveLen);

inline void custom(int16_t* buf, uint32_t numWaves, uint32_t waveLen,
                   CustomGenFn fn) {
    for (uint32_t w = 0; w < numWaves; ++w) {
        int16_t* dst = buf + w * waveLen;
        for (uint32_t s = 0; s < waveLen; ++s)
            dst[s] = floatToQ15(fn(w, s, numWaves, waveLen));
    }
}

// ---------------------------------------------------------------------------
// Audio capture — slice float audio into wavetable
// ---------------------------------------------------------------------------

/// Straight slice: first numWaves*waveLen samples → buffer.
inline void fromAudio(int16_t* buf, const float* audio, uint32_t audioLen,
                      uint32_t numWaves, uint32_t waveLen) {
    uint32_t needed = numWaves * waveLen;
    for (uint32_t i = 0; i < needed; ++i)
        buf[i] = floatToQ15(i < audioLen ? audio[i] : 0.0f);
}

/// Capture with zero-crossing alignment: tries to snap each wave start to
/// the nearest positive-going zero crossing for cleaner loops.
inline void fromAudioAligned(int16_t* buf, const float* audio,
                             uint32_t audioLen,
                             uint32_t numWaves, uint32_t waveLen) {
    uint32_t stride = (audioLen > waveLen)
        ? (audioLen - waveLen) / (numWaves > 1 ? numWaves - 1 : 1) : 0;
    for (uint32_t w = 0; w < numWaves; ++w) {
        uint32_t startGuess = w * stride;
        uint32_t start = startGuess;
        // Scan for positive-going zero crossing within one waveLen window
        uint32_t scanEnd = startGuess + waveLen;
        if (scanEnd >= audioLen) scanEnd = audioLen - 1;
        for (uint32_t i = startGuess; i < scanEnd; ++i) {
            if (audio[i] <= 0.0f && audio[i + 1] > 0.0f) {
                start = i + 1;
                break;
            }
        }
        int16_t* dst = buf + w * waveLen;
        for (uint32_t s = 0; s < waveLen; ++s) {
            uint32_t idx = start + s;
            dst[s] = floatToQ15(idx < audioLen ? audio[idx] : 0.0f);
        }
    }
}

} // namespace WtGen

#endif // LOFI_PARTS_WAVETABLE_GENERATOR_H
