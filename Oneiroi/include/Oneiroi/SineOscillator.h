#ifndef SINE_OSCILLATOR_H
#define SINE_OSCILLATOR_H

#include "Oscillator.h"
#include <cmath>
#include <cstddef>

constexpr std::size_t TABLE_SIZE = 2048;
constexpr float TWO_PI = 2.0f * static_cast<float>(M_PI);

// Static sine table, initialized once in a thread-safe way
inline const float* getSineTable() {
    static float sineTable[TABLE_SIZE];
    static bool initialized = false;
    if (!initialized) {
        for (std::size_t i = 0; i < TABLE_SIZE; ++i) {
            sineTable[i] = sinf(TWO_PI * i / TABLE_SIZE);
        }
        initialized = true;
    }
    return sineTable;
}

// Fast sine using the lookup table
inline float custom_sinf(float x) {
    float normalized_x = fmodf(x, TWO_PI);
    if (normalized_x < 0) normalized_x += TWO_PI;
    float pos = normalized_x * (TABLE_SIZE / TWO_PI);
    std::size_t idx1 = static_cast<std::size_t>(pos);
    std::size_t idx2 = (idx1 + 1) % TABLE_SIZE;
    float frac = pos - idx1;
    const float* table = getSineTable();
    return table[idx1] + frac * (table[idx2] - table[idx1]);
}

class SineOscillator : public OscillatorTemplate<SineOscillator> {
public:
    static constexpr float begin_phase = 0.0f;
    static constexpr float end_phase = TWO_PI;

    SineOscillator() = default;
    explicit SineOscillator(float sr) { setSampleRate(sr); }

    float getSample() { return custom_sinf(phase); }

    void generate(FloatArray output) {
        std::size_t len = output.getSize();
        for (std::size_t i = 0; i < len; ++i) {
            output[i] = custom_sinf(phase);
            phase += incr;
        }
        // Only wrap phase once per block
        if (phase >= end_phase) phase = fmodf(phase, end_phase);
    }

    void generate(FloatArray output, FloatArray fm) {
        std::size_t len = output.getSize();
        for (std::size_t i = 0; i < len; ++i) {
            output[i] = custom_sinf(phase);
            phase += incr * (1.0f + fm[i]);
        }
        if (phase >= end_phase) phase = fmodf(phase, end_phase);
    }

    using OscillatorTemplate<SineOscillator>::generate;
};

#endif /* SINE_OSCILLATOR_H */
