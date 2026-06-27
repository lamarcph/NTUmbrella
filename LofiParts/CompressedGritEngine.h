#ifndef LOFI_PARTS_COMPRESSED_GRIT_ENGINE_H
#define LOFI_PARTS_COMPRESSED_GRIT_ENGINE_H
#include "CheapMaths.h"
#include "Polyphase.h"

class CompressedGritEngine {
public:
    void init(float sampleRate) {
        fs = sampleRate;
        
        // Initialize high-band oversampling paths
        oversamplerL.init();
        oversamplerR.init();

        // Calculate sample-rate independent ballistics coefficients
        // Targets: 15ms Attack time, 200ms Release time
        attackAlpha = std::exp(-1.0f / (fs * 0.015f));
        releaseAlpha = std::exp(-1.0f / (fs * 0.200f));

        // Keep hi-band grit noise color stable across sample rates.
        // One-pole LP target ~800 Hz regardless of fs.
        const float hiGritCutoffHz = 800.0f;
        hiGritNoiseAlpha = std::exp(-2.0f * 3.14159265359f * hiGritCutoffHz / fs);

        for (int i = 0; i < 3; ++i) {
            envState[i] = 0.0f;
        }
    }

    void processStereoBlock(float* inputL, float* inputR, float* outputL, float* outputR, int numSamples,
                             float driveKnob, float lowRoom, float midRoom, float highRoom, float noiseBlend) 
    {
        // Pre-calculate expansion/compression curves at block rate to eliminate divisions per sample
        float compRatios[3] = {
            (lowRoom > 0.0f) ? (1.0f / (1.0f + lowRoom * 7.0f) - 1.0f) : (std::abs(lowRoom) * 2.0f),
            (midRoom > 0.0f) ? (1.0f / (1.0f + midRoom * 7.0f) - 1.0f) : (std::abs(midRoom) * 2.0f),
            (highRoom > 0.0f) ? (1.0f / (1.0f + highRoom * 7.0f) - 1.0f) : (std::abs(highRoom) * 2.0f)
        };
        
        const float thresholddB = -18.0f; 

        for (int n = 0; n < numSamples; ++n) {
            float l = inputL[n];
            float r = inputR[n];

            // --- Phase 1: 4th-Order Linkwitz-Riley Crossover Split ---
            // Note: Replace these placeholders with your runtime LR4 biquad execution matrix
            float lowL = l * 0.33f; 
            float midL = l * 0.33f;
            float highL = l * 0.33f;
            
            float lowR = r * 0.33f;
            float midR = r * 0.33f;
            float highR = r * 0.33f;

            // --- Phase 2: Interleaved Dynamics & Envelope Tracking ---
            float bandsL[3] = { lowL, midL, highL };
            float bandsR[3] = { lowR, midR, highR };
            float roomControls[3] = { lowRoom, midRoom, highRoom };

            for (int b = 0; b < 3; ++b) {
                float monoRectified = std::abs(bandsL[b] + bandsR[b]) * 0.5f;
                
                // Rate-corrected ballistics filter switching based on transient velocity
                float alpha = (monoRectified > envState[b]) ? attackAlpha : releaseAlpha;
                envState[b] = (1.0f - alpha) * monoRectified + alpha * envState[b];
                
                // Force flush-to-zero to prevent ARM Cortex-M7 subnormal performance degradation
                if (envState[b] < 1e-25f) envState[b] = 0.0f;

                // Branchless fast log approximation (utilizes type-punning casting from header)
                float envdB = 6.0206f * branchless_fast_log2f(envState[b] + 1e-5f);
                float overshootdB = envdB - thresholddB;
                float gaindB = 0.0f;

                if (roomControls[b] > 0.0f) {
                    // Upward Compression Routing
                    if (overshootdB > 0.0f) gaindB = overshootdB * compRatios[b];
                } else {
                    // Downward Expansion Routing
                    if (overshootdB < 0.0f) gaindB = overshootdB * compRatios[b];
                }

                // Reconstruct linear gain multipliers via fast_exp2f primitives
                // 10^(dB/20) == 2^(dB * 0.166096)
                float gainMultiplier = fast_exp2f(gaindB * 0.166096f);
                
                bandsL[b] *= gainMultiplier;
                bandsR[b] *= gainMultiplier;
            }

            // --- Phase 3: Targeted Harmonic Saturation Stage ---
            
            // Band 0 (Low): Symmetric low-end saturation using your Padé cubic approximation
            float lowDrive = 1.0f + (driveKnob * 2.0f);
            lowL = cheap_saturate(bandsL[0] * lowDrive); 
            lowR = cheap_saturate(bandsR[0] * lowDrive);

            // Band 1 (Mid): Asymmetric valve simulation generating even-order harmonics
            float midDrive = 1.0f + driveKnob;
            float drivenMidL = bandsL[1] * midDrive;
            float drivenMidR = bandsR[1] * midDrive;
            midL = drivenMidL + 0.18f * (drivenMidL * drivenMidL);
            midR = drivenMidR + 0.18f * (drivenMidR * drivenMidR);
            midL = clampf(midL, -0.95f, 0.95f);
            midR = clampf(midR, -0.95f, 0.95f);

            // Band 2 (High): Textured Noise Modulation & Rate-Guarded Clipper
            if (noiseBlend > 0.0f) {
                float rawNoise = uint16_to_float(xorshift16(), -1.0f, 1.0f);
                bandsL[2] += rawNoise * noiseBlend * envState[2];
                bandsR[2] += rawNoise * noiseBlend * envState[2];
            }

            float highDrive = 1.0f + (driveKnob * 1.2f);

            // Critical Sample-Rate Check: Avoid oversampling if operating natively at High Rates
            if (fs >= 88200.0f) {
                // At 96kHz base rate, native headroom renders oversampling redundant
                highL = clampCubic(bandsL[2] * highDrive);
                highR = clampCubic(bandsR[2] * highDrive);
            } else {
                // Allocate 2x oversampled register space
                float highL_Evn, highL_Odd;
                float highR_Evn, highR_Odd;

                // Decompose 44.1kHz / 48kHz signals into the 2x polyphase domain
                oversamplerL.upsample(bandsL[2], highL_Evn, highL_Odd);
                oversamplerR.upsample(bandsR[2], highR_Evn, highR_Odd);

                // Run saturation independently across the interleaved high-rate sub-samples
                highL_Evn = clampCubic(highL_Evn * highDrive);
                highL_Odd = clampCubic(highL_Odd * highDrive);
                highR_Evn = clampCubic(highR_Evn * highDrive);
                highR_Odd = clampCubic(highR_Odd * highDrive);

                // Reconstruct to base rate, filtering out generated components above original Nyquist bounds
                highL = oversamplerL.downsample(highL_Evn, highL_Odd);
                highR = oversamplerR.downsample(highR_Evn, highR_Odd);
            }

            // --- Phase 4: Stereo Output Recombination Matrix ---
            outputL[n] = clampf(lowL + midL + highL, -1.0f, 1.0f);
            outputR[n] = clampf(lowR + midR + highR, -1.0f, 1.0f);
        }
    }

    // processBandsInPlace: takes pre-split band signals (from an external LR4
    // crossover), applies per-band dynamics and saturation, and writes the
    // processed bands back in-place.  The caller is responsible for
    // recombining the bands into a mono/stereo output.
    //
    // Restructured into separate per-band passes so the compiler can optimize
    // each loop independently.  The hi-band 2x oversampling runs block-level:
    // upsample-all → scale+clip (stateless, vectorizable) → downsample-all,
    // instead of the original per-sample interleaved approach.
    void processBandsInPlace(
        float* loL, float* loR,
        float* midL, float* midR,
        float* hiL,  float* hiR,
        int numSamples,
        float driveKnob, float lowRoom, float midRoom, float highRoom,
        float gritLo, float gritMid, float gritHi,
        bool loAsym = false)
    {
        float compRatios[3] = {
            (lowRoom  > 0.0f) ? (1.0f / (1.0f + lowRoom  * 7.0f) - 1.0f) : (std::abs(lowRoom)  * 2.0f),
            (midRoom  > 0.0f) ? (1.0f / (1.0f + midRoom  * 7.0f) - 1.0f) : (std::abs(midRoom)  * 2.0f),
            (highRoom > 0.0f) ? (1.0f / (1.0f + highRoom * 7.0f) - 1.0f) : (std::abs(highRoom) * 2.0f)
        };

        const float thresholddB = -18.0f;
        const float lowDrive  = 1.0f + (driveKnob * 3.5f);
        const float midDrive  = 1.0f + (driveKnob * 1.7f);
        const float highDrive = 1.0f + (driveKnob * 2.2f);

        // ---- Pass 1: Lo band — compression + saturation + grit ----
        // envState[0] is independent of bands 1 and 2; separate loop allows
        // the compiler to schedule FPU ops for lo without band-interleave overhead.
        for (int n = 0; n < numSamples; ++n) {
            float monoRectified = std::abs(loL[n] + loR[n]) * 0.5f;
            float alpha = (monoRectified > envState[0]) ? attackAlpha : releaseAlpha;
            envState[0] = (1.0f - alpha) * monoRectified + alpha * envState[0];
            if (envState[0] < 1e-25f) envState[0] = 0.0f;
            float envdB = 6.0206f * branchless_fast_log2f(envState[0] + 1e-5f);
            float overshootdB = envdB - thresholddB;
            float gaindB = 0.0f;
            if (lowRoom > 0.0f) { if (overshootdB > 0.0f) gaindB = overshootdB * compRatios[0]; }
            else                 { if (overshootdB < 0.0f) gaindB = overshootdB * compRatios[0]; }
            float gain = fast_exp2f(gaindB * 0.166096f);
            loL[n] *= gain; loR[n] *= gain;

            float lSatL, lSatR;
            if (loAsym) {
                const float dL = loL[n] * lowDrive; const float dR = loR[n] * lowDrive;
                lSatL = clampf(dL + 0.10f * dL * dL, -0.95f, 0.95f);
                lSatR = clampf(dR + 0.10f * dR * dR, -0.95f, 0.95f);
            } else {
                lSatL = cheap_saturate(loL[n] * lowDrive);
                lSatR = cheap_saturate(loR[n] * lowDrive);
            }
            // Lo grit: wave fold after saturation — adds dense harmonic content
            if (gritLo > 0.0f) {
                const float ft = 1.0f - gritLo * 0.6f;
                if (lSatL >  ft) lSatL = 2.0f * ft - lSatL;
                if (lSatL < -ft) lSatL = -2.0f * ft - lSatL;
                if (lSatR >  ft) lSatR = 2.0f * ft - lSatR;
                if (lSatR < -ft) lSatR = -2.0f * ft - lSatR;
            }
            loL[n] = lSatL; loR[n] = lSatR;
        }

        // ---- Pass 2: Mid band — compression + saturation + grit ----
        for (int n = 0; n < numSamples; ++n) {
            float monoRectified = std::abs(midL[n] + midR[n]) * 0.5f;
            float alpha = (monoRectified > envState[1]) ? attackAlpha : releaseAlpha;
            envState[1] = (1.0f - alpha) * monoRectified + alpha * envState[1];
            if (envState[1] < 1e-25f) envState[1] = 0.0f;
            float envdB = 6.0206f * branchless_fast_log2f(envState[1] + 1e-5f);
            float overshootdB = envdB - thresholddB;
            float gaindB = 0.0f;
            if (midRoom > 0.0f) { if (overshootdB > 0.0f) gaindB = overshootdB * compRatios[1]; }
            else                 { if (overshootdB < 0.0f) gaindB = overshootdB * compRatios[1]; }
            float gain = fast_exp2f(gaindB * 0.166096f);
            midL[n] *= gain; midR[n] *= gain;

            // Mid: asymmetric valve saturation + optional wave fold for mid grit
            const float dMidL = midL[n] * midDrive; const float dMidR = midR[n] * midDrive;
            float mSatL = clampf(dMidL + 0.18f * dMidL * dMidL, -0.95f, 0.95f);
            float mSatR = clampf(dMidR + 0.18f * dMidR * dMidR, -0.95f, 0.95f);
            if (gritMid > 0.0f) {
                const float ft = 0.95f - gritMid * 0.60f;
                if (mSatL >  ft) mSatL = 2.0f * ft - mSatL;
                if (mSatL < -ft) mSatL = -2.0f * ft - mSatL;
                if (mSatR >  ft) mSatR = 2.0f * ft - mSatR;
                if (mSatR < -ft) mSatR = -2.0f * ft - mSatR;
                mSatL = clampf(mSatL, -0.95f, 0.95f); mSatR = clampf(mSatR, -0.95f, 0.95f);
            }
            midL[n] = mSatL; midR[n] = mSatR;
        }

        // ---- Pass 3a: Hi band — compression only ----
        for (int n = 0; n < numSamples; ++n) {
            float monoRectified = std::abs(hiL[n] + hiR[n]) * 0.5f;
            float alpha = (monoRectified > envState[2]) ? attackAlpha : releaseAlpha;
            envState[2] = (1.0f - alpha) * monoRectified + alpha * envState[2];
            if (envState[2] < 1e-25f) envState[2] = 0.0f;
            float envdB = 6.0206f * branchless_fast_log2f(envState[2] + 1e-5f);
            float overshootdB = envdB - thresholddB;
            float gaindB = 0.0f;
            if (highRoom > 0.0f) { if (overshootdB > 0.0f) gaindB = overshootdB * compRatios[2]; }
            else                  { if (overshootdB < 0.0f) gaindB = overshootdB * compRatios[2]; }
            float gain = fast_exp2f(gaindB * 0.166096f);
            hiL[n] *= gain; hiR[n] *= gain;
        }

        // ---- Pass 3b: Hi band — oversampled saturation ----
        // Separate loop from compression (pass 3a) so the compiler can schedule each cleanly.
        // Per-sample upsample→clip→downsample: avoids large stack temp buffers that would
        // overflow the NT audio thread's limited stack when called from step().
        if (fs >= 88200.0f) {
            for (int n = 0; n < numSamples; ++n) {
                hiL[n] = clampCubic(hiL[n] * highDrive);
                hiR[n] = clampCubic(hiR[n] * highDrive);
            }
        } else {
            for (int n = 0; n < numSamples; ++n) {
                float hL_e, hL_o, hR_e, hR_o;
                oversamplerL.upsample(hiL[n], hL_e, hL_o);
                oversamplerR.upsample(hiR[n], hR_e, hR_o);
                hL_e = clampCubic(hL_e * highDrive); hL_o = clampCubic(hL_o * highDrive);
                hR_e = clampCubic(hR_e * highDrive); hR_o = clampCubic(hR_o * highDrive);
                hiL[n] = oversamplerL.downsample(hL_e, hL_o);
                hiR[n] = oversamplerR.downsample(hR_e, hR_o);
            }
        }

        // ---- Pass 3c: Hi grit — 1-pole LP-filtered noise shimmer ----
        // Kept as a separate pass: hiGritNoise has serial state across samples.
        if (gritHi > 0.0f) {
            for (int n = 0; n < numSamples; ++n) {
                const float raw = uint16_to_float(xorshift16(), -1.0f, 1.0f);
                hiGritNoise = hiGritNoise * hiGritNoiseAlpha + raw * (1.0f - hiGritNoiseAlpha);
                const float shimmerL = hiGritNoise * gritHi * 0.4f * (std::fabs(hiL[n]) + 0.02f);
                const float shimmerR = hiGritNoise * gritHi * 0.4f * (std::fabs(hiR[n]) + 0.02f);
                hiL[n] = clampf(hiL[n] + shimmerL, -1.5f, 1.5f);
                hiR[n] = clampf(hiR[n] + shimmerR, -1.5f, 1.5f);
            }
        }
    }

private:
    float fs;
    float hiGritNoise = 0.0f;
    float hiGritNoiseAlpha = 0.9f;
    float attackAlpha;
    float releaseAlpha;
    float envState[3];
    Polyphase2xEngine oversamplerL;
    Polyphase2xEngine oversamplerR;

    // Strict local branchless implementation of fast_log2f to guarantee vector/pipeline optimization
    static inline float branchless_fast_log2f(float x) {
        fm_float_cast u = { .f = x };
        int32_t exponent = ((u.i >> 23) & 0xFF) - 127;
        
        u.i &= 0x007FFFFF;
        u.i |= 0x3F800000;
        
        const int32_t mantissa_bits = u.i & 0x007FFFFF;
        const int32_t index = mantissa_bits >> (23 - FM_LUT_BITS);
        const float frac = (float)(mantissa_bits & 0x0001FFFF) * (1.0f / 131072.0f);
        
        float y0 = _fm_log2_lut[index];
        float y1 = _fm_log2_lut[index + 1];
        
        return (float)exponent + y0 + frac * (y1 - y0);
    }

    // Inlined embedded-optimized cubic soft clipper clamping strictly at unity bounds
    static inline float clampCubic(float x) {
        if (x > 1.2f) return 1.0f;
        if (x < -1.2f) return -1.0f;
        return x - (0.333333f * x * x * x);
    }
};

#endif // LOFI_PARTS_COMPRESSED_GRIT_ENGINE_H