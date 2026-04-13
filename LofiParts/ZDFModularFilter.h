#pragma once
#include <algorithm>
#include <cmath>

enum FilterMode { LP, HP, BP, NOTCH, BYPASS };

class ZDFModularFilter {
public:
    ZDFModularFilter() : s1(0.0f), s2(0.0f), sampleRate(48000.0f), mode(LP) {
        // Initialize coefficients to a neutral state
        a1 = a2 = a3 = damp = 0.0f;
    }

    void setMode(FilterMode m) { mode = m; }
    void setSampleRate(float sr) { sampleRate = sr; }

    /**
     * processBlock(): High-performance inner loop with linear coefficient interpolation.
     * targetCutoff: Frequency in Hz at the END of this block.
     * resonance: 0.0 to 1.0.
     * drive: 1.0 (clean) to 10.0 (heavy saturation).
     */
    void processBlock(const float* input, float* output, int numSamples, 
                      float targetCutoff, float resonance, float drive) {
        
        if (mode == BYPASS) {
            for (int i = 0; i < numSamples; ++i) output[i] = input[i];
            return;
        }

        // 1. Calculate TARGET coefficients (The "Destination")
        float cutoff = std::min(targetCutoff, sampleRate * 0.45f);
        float g_target = std::tan(3.1415926535f * cutoff / sampleRate);
        float damp_target = 2.0f - (2.0f * std::min(std::max(resonance, 0.0f), 0.99f));
        
        float den = 1.0f / (1.0f + g_target * (g_target + damp_target));
        float a1_target = den;
        float a2_target = g_target * a1_target;
        float a3_target = g_target * a2_target;

        // 2. Calculate interpolation STEPS per sample
        // This spreads the 'jump' across the whole block
        float invN = 1.0f / (float)numSamples;
        float a1_step = (a1_target - a1) * invN;
        float a2_step = (a2_target - a2) * invN;
        float a3_step = (a3_target - a3) * invN;
        float damp_step = (damp_target - damp) * invN;

        // Cache state and drive reciprocal
        float ls1 = s1; float ls2 = s2;
        float invDrive = 1.0f / drive;

        for (int i = 0; i < numSamples; ++i) {
            // 3. Incrementally smooth coefficients
            a1 += a1_step;
            a2 += a2_step;
            a3 += a3_step;
            damp += damp_step;

            float in = input[i];
            
            // 4. Calculate v3 with Drive and Cubic Saturation
            float v3 = (in - ls2) * drive;

            // Soft-clipper: x - (x^3 / 5) for a slightly warmer NL2 flavor
            if (v3 > 1.25f) v3 = 0.8333f;
            else if (v3 < -1.25f) v3 = -0.8333f;
            else v3 = v3 - (v3 * v3 * v3 * 0.2f);

            float v3_comp = v3 * invDrive;

            // 5. TPT Update Equations using current interpolated coeffs
            float v1 = a1 * ls1 + a2 * v3_comp;
            float v2 = ls2 + a2 * ls1 + a3 * v3_comp;

            // 6. Update State Registers
            ls1 = 2.0f * v1 - ls1;
            ls2 = 2.0f * v2 - ls2;

            // 7. Output Node Selection
            switch (mode) {
                case LP:    output[i] = v2; break;
                case BP:    output[i] = v1; break;
                case HP:    output[i] = in - damp * v1 - v2; break;
                case NOTCH: output[i] = in - damp * v1; break;
                default:    output[i] = v2; break;
            }
        }

        // Store registers back to members
        s1 = ls1; s2 = ls2;
        // Important: a1, a2, a3, damp are now at their target values for the next block
    }

    void reset() { s1 = s2 = 0.0f; }

    void snapToTarget(float cutoffHz, float resonance) {
        float cutoff = std::min(cutoffHz, sampleRate * 0.45f);
        float g_target = std::tan(3.1415926535f * cutoff / sampleRate);
        float damp_target = 2.0f - (2.0f * std::min(std::max(resonance, 0.0f), 0.99f));
        
        float den = 1.0f / (1.0f + g_target * (g_target + damp_target));
        a1 = den;
        a2 = g_target * a1;
        a3 = g_target * a2;
        damp = damp_target;
        
        // Clear state to ensure a clean start
        s1 = s2 = 0.0f;
    }

private:
    float s1, s2;           // Integrator states
    float a1, a2, a3, damp; // Current coefficients (interpolated)
    float sampleRate;
    FilterMode mode;
};