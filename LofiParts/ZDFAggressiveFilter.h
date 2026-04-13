#pragma once
#include <algorithm>
#include <cmath>
#include "CheapMaths.h" // Assuming your LUT-based fast math is here

enum class FilterMode { LP2, LP4, HP2, BP2, NOTCH2, HP2_LP2, BYPASS };

class ZDFAggressiveFilter {
public:
    ZDFAggressiveFilter() : sampleRate(48000.0f) {
        reset();
    }

    void setSampleRate(float sr) { sampleRate = sr; }

    void reset() {
        s1_a = s2_a = s1_b = s2_b = 0.0f;
        a1 = a2 = a3 = damp = 0.0f;
    }

    void processBlock(const float* input, float* output, int numSamples, 
                      float targetCutoff, float resonance, float drive, FilterMode mode) {
        
        if (mode == FilterMode::BYPASS) {
            for (int i = 0; i < numSamples; ++i) output[i] = input[i];
            return;
        }

        // 1. Calculate Target Coefficients
        float cutoff = std::min(targetCutoff, sampleRate * 0.45f);
        float g_target = std::tan(3.1415926535f * cutoff / sampleRate);
        
        // Non-linear damping scaling to increase "screech" at high resonance
        // 0.0 resonance = 2.0 damp, 1.0 resonance = 0.0 damp
        float damp_target = 2.0f * (1.0f - std::min(resonance, 0.999f));
        
        float den = 1.0f / (1.0f + g_target * (g_target + damp_target));
        float a1_target = den;
        float a2_target = g_target * a1_target;
        float a3_target = g_target * a2_target;

        // 2. Interpolation Steps
        float invN = 1.0f / (float)numSamples;
        float a1_step = (a1_target - a1) * invN;
        float a2_step = (a2_target - a2) * invN;
        float a3_step = (a3_target - a3) * invN;
        float damp_step = (damp_target - damp) * invN;

        // 3. Optimized Dispatcher
        switch (mode) {
            case FilterMode::LP2:       render<FilterMode::LP2>(input, output, numSamples, a1_step, a2_step, a3_step, damp_step, drive); break;
            case FilterMode::LP4:       render<FilterMode::LP4>(input, output, numSamples, a1_step, a2_step, a3_step, damp_step, drive); break;
            case FilterMode::HP2:       render<FilterMode::HP2>(input, output, numSamples, a1_step, a2_step, a3_step, damp_step, drive); break;
            case FilterMode::BP2:       render<FilterMode::BP2>(input, output, numSamples, a1_step, a2_step, a3_step, damp_step, drive); break;
            case FilterMode::NOTCH2:    render<FilterMode::NOTCH2>(input, output, numSamples, a1_step, a2_step, a3_step, damp_step, drive); break;
            case FilterMode::HP2_LP2:   render<FilterMode::HP2_LP2>(input, output, numSamples, a1_step, a2_step, a3_step, damp_step, drive); break;
            default: break;
        }
    }

private:
    // Core TPT State Update (Inlined for speed)
    // Mode-specific tapping happens here
    inline float processStage(float in, float& s1, float& s2, float drive, FilterMode m) {
        float v3 = in - s2;
        
        // Non-linear feedback loop: Drive affects the 'error' signal
        float v3_sat = cheap_saturate(v3 * drive); 

        float v1 = a1 * s1 + a2 * v3_sat;
        float v2 = s2 + a2 * s1 + a3 * v3_sat;

        // Update state registers
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;

        // Node Selection
        if (m == FilterMode::LP2 || m == FilterMode::LP4) return v2;
        if (m == FilterMode::BP2) return v1;
        if (m == FilterMode::HP2) return in - (damp * v1) - v2;
        if (m == FilterMode::NOTCH2) return in - (damp * v1);
        return v2;
    }

    template <FilterMode M>
    void render(const float* input, float* output, int numSamples, 
                float a1_s, float a2_s, float a3_s, float d_s, float drive) {
        
        for (int i = 0; i < numSamples; ++i) {
            a1 += a1_s; a2 += a2_s; a3 += a3_s; damp += d_s;

            if constexpr (M == FilterMode::LP4) {
                float stage1 = processStage(input[i], s1_a, s2_a, drive, FilterMode::LP2);
                output[i] = processStage(stage1, s1_b, s2_b, drive, FilterMode::LP2);
            } 
            else if constexpr (M == FilterMode::HP2_LP2) {
                float stage1 = processStage(input[i], s1_a, s2_a, drive, FilterMode::HP2);
                output[i] = processStage(stage1, s1_b, s2_b, drive, FilterMode::LP2);
            }
            else {
                output[i] = processStage(input[i], s1_a, s2_a, drive, M);
            }
        }
    }

    float s1_a, s2_a, s1_b, s2_b;
    float a1, a2, a3, damp;
    float sampleRate;
};