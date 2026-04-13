// =============================================================================
// ZDFFilter.h — Unified ZDF filter with four models
// =============================================================================
// Approach B: single class, model switch dispatches to the active topology.
// Only one model runs at a time — zero extra CPU/memory overhead.
//
// Models:
//   SVF    — 2-pole TPT State Variable (from ZDFAggressiveFilter). All 7 modes.
//   LADDER — 4-pole TPT Moog-style ladder. LP4 is the signature sound.
//            Other modes derived from ladder tap mixing.
//   MS20   — 2-pole Sallen-Key with saturated feedback (Korg MS-20 character).
//            Cascaded to 4-pole for LP4. Screaming resonance from feedback
//            distortion, unlike SVF (forward-path) or Ladder (input).
//   DIODE  — 4-pole TPT diode ladder (TB-303 inspired). Asymmetric per-stage
//            clipping preserves bass at high resonance. Squelchy acid sound.
//
// State: 4 floats (SVF uses two 2-pole stages; Ladder/MS20/Diode use four
//        1-pole stages).
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include "CheapMaths.h"

enum class FilterMode { LP2, LP4, HP2, BP2, NOTCH2, HP2_LP2, BYPASS };
enum class FilterModel { SVF, LADDER, MS20, DIODE };

class ZDFFilter {
public:
    ZDFFilter() : model_(FilterModel::SVF), sampleRate_(48000.0f) { reset(); }

    void setSampleRate(float sr) { sampleRate_ = sr; }

    void setModel(FilterModel m) {
        if (m != model_) { model_ = m; reset(); }
    }
    FilterModel getModel() const { return model_; }

    void reset() {
        s1_ = s2_ = s3_ = s4_ = 0.0f;
        svf_a1_ = svf_a2_ = svf_a3_ = svf_damp_ = 0.0f;
        ladder_g_ = 0.0f;
        ladder_k_ = 0.0f;
    }

    void processBlock(const float* input, float* output, int numSamples,
                      float targetCutoff, float resonance, float drive, FilterMode mode) {
        if (mode == FilterMode::BYPASS) {
            for (int i = 0; i < numSamples; ++i) output[i] = input[i];
            return;
        }
        switch (model_) {
            case FilterModel::LADDER: processBlockLadder(input, output, numSamples, targetCutoff, resonance, drive, mode); break;
            case FilterModel::MS20:   processBlockMS20(input, output, numSamples, targetCutoff, resonance, drive, mode);   break;
            case FilterModel::DIODE:  processBlockDiode(input, output, numSamples, targetCutoff, resonance, drive, mode);  break;
            default:                  processBlockSVF(input, output, numSamples, targetCutoff, resonance, drive, mode);    break;
        }
    }

private:
    // =====================================================================
    // SVF (State Variable Filter) — original ZDFAggressiveFilter topology
    // =====================================================================
    void processBlockSVF(const float* input, float* output, int numSamples,
                         float targetCutoff, float resonance, float drive, FilterMode mode) {
        float cutoff = std::min(targetCutoff, sampleRate_ * 0.45f);
        float g_target = std::tan(3.1415926535f * cutoff / sampleRate_);
        float damp_target = 2.0f * (1.0f - std::min(resonance, 0.999f));

        float den = 1.0f / (1.0f + g_target * (g_target + damp_target));
        float a1_target = den;
        float a2_target = g_target * a1_target;
        float a3_target = g_target * a2_target;

        float invN = 1.0f / (float)numSamples;
        float a1_step = (a1_target - svf_a1_) * invN;
        float a2_step = (a2_target - svf_a2_) * invN;
        float a3_step = (a3_target - svf_a3_) * invN;
        float damp_step = (damp_target - svf_damp_) * invN;

        switch (mode) {
            case FilterMode::LP2:     renderSVF<FilterMode::LP2>(input, output, numSamples, a1_step, a2_step, a3_step, damp_step, drive); break;
            case FilterMode::LP4:     renderSVF<FilterMode::LP4>(input, output, numSamples, a1_step, a2_step, a3_step, damp_step, drive); break;
            case FilterMode::HP2:     renderSVF<FilterMode::HP2>(input, output, numSamples, a1_step, a2_step, a3_step, damp_step, drive); break;
            case FilterMode::BP2:     renderSVF<FilterMode::BP2>(input, output, numSamples, a1_step, a2_step, a3_step, damp_step, drive); break;
            case FilterMode::NOTCH2:  renderSVF<FilterMode::NOTCH2>(input, output, numSamples, a1_step, a2_step, a3_step, damp_step, drive); break;
            case FilterMode::HP2_LP2: renderSVF<FilterMode::HP2_LP2>(input, output, numSamples, a1_step, a2_step, a3_step, damp_step, drive); break;
            default: break;
        }
    }

    inline float processSVFStage(float in, float& s1, float& s2, float drive, FilterMode m) {
        float v3 = in - s2;
        float v3_sat = cheap_saturate(v3 * drive);

        float v1 = svf_a1_ * s1 + svf_a2_ * v3_sat;
        float v2 = s2 + svf_a2_ * s1 + svf_a3_ * v3_sat;

        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;

        if (m == FilterMode::LP2 || m == FilterMode::LP4) return v2;
        if (m == FilterMode::BP2) return v1;
        if (m == FilterMode::HP2) return in - (svf_damp_ * v1) - v2;
        if (m == FilterMode::NOTCH2) return in - (svf_damp_ * v1);
        return v2;
    }

    template <FilterMode M>
    void renderSVF(const float* input, float* output, int numSamples,
                   float a1_s, float a2_s, float a3_s, float d_s, float drive) {
        for (int i = 0; i < numSamples; ++i) {
            svf_a1_ += a1_s; svf_a2_ += a2_s; svf_a3_ += a3_s; svf_damp_ += d_s;

            if constexpr (M == FilterMode::LP4) {
                float stage1 = processSVFStage(input[i], s1_, s2_, drive, FilterMode::LP2);
                output[i] = processSVFStage(stage1, s3_, s4_, drive, FilterMode::LP2);
            }
            else if constexpr (M == FilterMode::HP2_LP2) {
                float stage1 = processSVFStage(input[i], s1_, s2_, drive, FilterMode::HP2);
                output[i] = processSVFStage(stage1, s3_, s4_, drive, FilterMode::LP2);
            }
            else {
                output[i] = processSVFStage(input[i], s1_, s2_, drive, M);
            }
        }
    }

    // =====================================================================
    // Ladder (Moog-style 4-pole TPT with resolved feedback)
    // =====================================================================
    // Based on Zavalishin's "The Art of VA Filter Design" — ZDF ladder with
    // analytically resolved implicit feedback. Four cascaded TPT one-pole
    // integrators with output-to-input resonance path.
    //
    // Mode mapping:
    //   LP4    — y4 (classic Moog 24dB/oct)
    //   LP2    — y2 (12dB/oct with ladder character)
    //   BP2    — y2 - y4 (resonant bandpass from tap difference)
    //   HP2    — u - y2 (highpass derived from input minus LP2)
    //   NOTCH2 — u - y2 + y4 (notch from complementary taps)
    //   HP2_LP2 — y4 (falls back to LP4)
    // =====================================================================
    void processBlockLadder(const float* input, float* output, int numSamples,
                            float targetCutoff, float resonance, float drive, FilterMode mode) {
        float cutoff = std::min(targetCutoff, sampleRate_ * 0.45f);
        float g_target = std::tan(3.1415926535f * cutoff / sampleRate_);
        float k_target = resonance * 3.98f; // 0–1 → 0–3.98 (just below self-oscillation)

        float invN = 1.0f / (float)numSamples;
        float g_step = (g_target - ladder_g_) * invN;
        float k_step = (k_target - ladder_k_) * invN;

        switch (mode) {
            case FilterMode::LP4:     renderLadder<FilterMode::LP4>(input, output, numSamples, g_step, k_step, drive); break;
            case FilterMode::LP2:     renderLadder<FilterMode::LP2>(input, output, numSamples, g_step, k_step, drive); break;
            case FilterMode::BP2:     renderLadder<FilterMode::BP2>(input, output, numSamples, g_step, k_step, drive); break;
            case FilterMode::HP2:     renderLadder<FilterMode::HP2>(input, output, numSamples, g_step, k_step, drive); break;
            case FilterMode::NOTCH2:  renderLadder<FilterMode::NOTCH2>(input, output, numSamples, g_step, k_step, drive); break;
            case FilterMode::HP2_LP2: renderLadder<FilterMode::LP4>(input, output, numSamples, g_step, k_step, drive); break;
            default: break;
        }
    }

    template <FilterMode M>
    void renderLadder(const float* input, float* output, int numSamples,
                      float g_step, float k_step, float drive) {
        for (int i = 0; i < numSamples; ++i) {
            ladder_g_ += g_step;
            ladder_k_ += k_step;

            float g = ladder_g_;
            float k = ladder_k_;
            float G = g / (1.0f + g);
            float G2 = G * G;
            float G3 = G2 * G;
            float G4 = G3 * G;

            // Predicted state contribution for implicit feedback resolution
            float sigma = G3 * s1_ + G2 * s2_ + G * s3_ + s4_;

            // Resolve feedback analytically: u = (in - k·(1-G)·sigma) / (1 + k·G⁴)
            float u = (input[i] - k * (1.0f - G) * sigma) / (1.0f + k * G4);

            // Drive saturation at ladder input
            u = cheap_saturate(u * drive);

            // Four cascaded TPT one-pole integrators
            float v, y1, y2, y3, y4;

            v = G * (u - s1_);
            y1 = v + s1_;
            s1_ = y1 + v;

            v = G * (y1 - s2_);
            y2 = v + s2_;
            s2_ = y2 + v;

            v = G * (y2 - s3_);
            y3 = v + s3_;
            s3_ = y3 + v;

            v = G * (y3 - s4_);
            y4 = v + s4_;
            s4_ = y4 + v;

            // Passband compensation: ladder inherently loses gain = 1/(1+k).
            // Partial compensation (0.6) keeps passband honest without
            // ballooning at high resonance where saturation already limits.
            float comp = 1.0f + k * 0.6f;

            // Output tap based on mode
            if constexpr (M == FilterMode::LP4)
                output[i] = y4 * comp;
            else if constexpr (M == FilterMode::LP2)
                output[i] = y2 * comp;
            else if constexpr (M == FilterMode::BP2)
                output[i] = (y2 - y4) * comp;
            else if constexpr (M == FilterMode::HP2)
                output[i] = (u - y2) * comp;
            else if constexpr (M == FilterMode::NOTCH2)
                output[i] = (u - y2 + y4) * comp;
            else
                output[i] = y4 * comp;

            (void)y1; (void)y3; // suppress unused warnings for non-LP4 paths
        }
    }

    // =====================================================================
    // MS-20 (Korg-style Sallen-Key with saturated feedback)
    // =====================================================================
    // The MS-20 character comes from saturation *in the resonance feedback
    // loop*. Unlike SVF (forward-path saturation) or Ladder (input
    // saturation), the MS-20 distorts the resonance signal itself,
    // creating a harsh, screaming quality at high feedback.
    //
    // 2-pole: two cascaded TPT 1-pole integrators with output→input
    //         feedback resolved analytically (ZDF, no delay).
    // 4-pole: two cascaded 2-pole SK sections.
    // =====================================================================
    void processBlockMS20(const float* input, float* output, int numSamples,
                          float targetCutoff, float resonance, float drive, FilterMode mode) {
        float cutoff = std::min(targetCutoff, sampleRate_ * 0.45f);
        float g_target = std::tan(3.1415926535f * cutoff / sampleRate_);
        float k_target = resonance * 4.0f;   // 0–1 → 0–4 (screaming)

        float invN = 1.0f / (float)numSamples;
        float g_step = (g_target - ladder_g_) * invN;
        float k_step = (k_target - ladder_k_) * invN;

        switch (mode) {
            case FilterMode::LP4:     renderMS20<FilterMode::LP4>(input, output, numSamples, g_step, k_step, drive); break;
            case FilterMode::LP2:     renderMS20<FilterMode::LP2>(input, output, numSamples, g_step, k_step, drive); break;
            case FilterMode::BP2:     renderMS20<FilterMode::BP2>(input, output, numSamples, g_step, k_step, drive); break;
            case FilterMode::HP2:     renderMS20<FilterMode::HP2>(input, output, numSamples, g_step, k_step, drive); break;
            case FilterMode::NOTCH2:  renderMS20<FilterMode::NOTCH2>(input, output, numSamples, g_step, k_step, drive); break;
            case FilterMode::HP2_LP2: renderMS20<FilterMode::LP4>(input, output, numSamples, g_step, k_step, drive); break;
            default: break;
        }
    }

    template <FilterMode M>
    void renderMS20(const float* input, float* output, int numSamples,
                    float g_step, float k_step, float drive) {
        for (int i = 0; i < numSamples; ++i) {
            ladder_g_ += g_step;
            ladder_k_ += k_step;
            float g = ladder_g_;
            float k = ladder_k_;
            float G = g / (1.0f + g);
            float G2 = G * G;

            // Drive at input
            float in = cheap_saturate(input[i] * drive);

            if constexpr (M == FilterMode::LP4) {
                // --- 4-pole global feedback (true MS-20 topology) ---
                // Same 4-integrator cascade as Moog ladder, but saturation
                // is in the feedback path (not at the input).  This makes
                // the resonance peak distort independently, creating the
                // characteristic MS-20 scream.
                float G3 = G2 * G;
                float G4 = G3 * G;

                // Predict y4 via linearised implicit feedback (Zavalishin)
                float sigma = G3 * s1_ + G2 * s2_ + G * s3_ + s4_;
                float u_lin = (in - k * (1.0f - G) * sigma) / (1.0f + k * G4);
                float y4_est = G4 * u_lin + (1.0f - G) * sigma;

                // Saturated feedback — THE MS-20 screaming character
                float fb = cheap_saturate(k * y4_est);
                float u = in - fb;

                // Four cascaded TPT integrators (no local feedback)
                float v, y1, y2, y3, y4;
                v = G * (u  - s1_); y1 = v + s1_; s1_ = y1 + v;
                v = G * (y1 - s2_); y2 = v + s2_; s2_ = y2 + v;
                v = G * (y2 - s3_); y3 = v + s3_; s3_ = y3 + v;
                v = G * (y3 - s4_); y4 = v + s4_; s4_ = y4 + v;

                output[i] = y4 * (1.0f + k * 0.6f);
                (void)y1; (void)y2; (void)y3;
            }
            else {
                // --- 2-pole modes: local Sallen-Key feedback ---
                float sigma1 = G * (1.0f - G) * s1_ + (1.0f - G) * s2_;
                float y2_est = (G2 * in + sigma1) / (1.0f + k * G2);

                // Saturated feedback
                float fb = cheap_saturate(k * y2_est);
                float u = in - fb;

                float v1 = G * (u - s1_);
                float y1 = v1 + s1_;
                s1_ = y1 + v1;

                float v2 = G * (y1 - s2_);
                float y2 = v2 + s2_;
                s2_ = y2 + v2;

                float comp = 1.0f + k * 0.5f;

                if constexpr (M == FilterMode::LP2)    output[i] = y2 * comp;
                else if constexpr (M == FilterMode::HP2)    output[i] = (in - y2) * comp;
                else if constexpr (M == FilterMode::BP2)    output[i] = (y1 - y2) * comp;
                else if constexpr (M == FilterMode::NOTCH2) output[i] = (in - y1 + y2) * comp;
                else                                        output[i] = y2 * comp;

                (void)y1;
            }
        }
    }

    // =====================================================================
    // Diode Ladder (TB-303 inspired)
    // =====================================================================
    // Same four-cascaded-TPT-integrator topology as the Moog ladder, but
    // with per-stage asymmetric diode clipping and reduced passband
    // compensation.  This preserves more bass at high resonance and adds
    // even-harmonic colour, giving the characteristic "squelchy" acid
    // character.
    //
    // diode_clip():  cheap_saturate(x + 0.3) − cheap_saturate(0.3)
    //   Shifted saturation curve → asymmetric positive/negative clipping
    //   while staying centered at zero.
    // =====================================================================
    static inline float diode_clip(float x) {
        return cheap_saturate(x + 0.3f) - 0.29222f;   // precomputed cheap_saturate(0.3)
    }

    void processBlockDiode(const float* input, float* output, int numSamples,
                           float targetCutoff, float resonance, float drive, FilterMode mode) {
        float cutoff = std::min(targetCutoff, sampleRate_ * 0.45f);
        float g_target = std::tan(3.1415926535f * cutoff / sampleRate_);
        float k_target = resonance * 3.5f;   // lower ceiling than Moog 3.98 → less self-osc

        float invN = 1.0f / (float)numSamples;
        float g_step = (g_target - ladder_g_) * invN;
        float k_step = (k_target - ladder_k_) * invN;

        switch (mode) {
            case FilterMode::LP4:     renderDiode<FilterMode::LP4>(input, output, numSamples, g_step, k_step, drive); break;
            case FilterMode::LP2:     renderDiode<FilterMode::LP2>(input, output, numSamples, g_step, k_step, drive); break;
            case FilterMode::BP2:     renderDiode<FilterMode::BP2>(input, output, numSamples, g_step, k_step, drive); break;
            case FilterMode::HP2:     renderDiode<FilterMode::HP2>(input, output, numSamples, g_step, k_step, drive); break;
            case FilterMode::NOTCH2:  renderDiode<FilterMode::NOTCH2>(input, output, numSamples, g_step, k_step, drive); break;
            case FilterMode::HP2_LP2: renderDiode<FilterMode::LP4>(input, output, numSamples, g_step, k_step, drive); break;
            default: break;
        }
    }

    template <FilterMode M>
    void renderDiode(const float* input, float* output, int numSamples,
                     float g_step, float k_step, float drive) {
        for (int i = 0; i < numSamples; ++i) {
            ladder_g_ += g_step;
            ladder_k_ += k_step;
            float g = ladder_g_;
            float k = ladder_k_;
            float G = g / (1.0f + g);
            float G2 = G * G;
            float G3 = G2 * G;
            float G4 = G3 * G;

            // Same analytic feedback resolution as Moog ladder
            float sigma = G3 * s1_ + G2 * s2_ + G * s3_ + s4_;
            float u = (input[i] - k * (1.0f - G) * sigma) / (1.0f + k * G4);

            // Drive + diode clipping at ladder input
            u = diode_clip(u * drive);

            // Four cascaded TPT integrators with per-stage diode clipping
            float v, y1, y2, y3, y4;

            v = G * (u - s1_);
            y1 = v + s1_;   s1_ = y1 + v;
            y1 = diode_clip(y1);             // diode between stage 1→2

            v = G * (y1 - s2_);
            y2 = v + s2_;   s2_ = y2 + v;
            y2 = diode_clip(y2);             // diode between stage 2→3

            v = G * (y2 - s3_);
            y3 = v + s3_;   s3_ = y3 + v;
            y3 = diode_clip(y3);             // diode between stage 3→4

            v = G * (y3 - s4_);
            y4 = v + s4_;   s4_ = y4 + v;

            // Less passband compensation than Moog — preserves bass
            float comp = 1.0f + k * 0.5f;

            if constexpr (M == FilterMode::LP4)
                output[i] = y4 * comp;
            else if constexpr (M == FilterMode::LP2)
                output[i] = y2 * comp;
            else if constexpr (M == FilterMode::BP2)
                output[i] = (y2 - y4) * comp;
            else if constexpr (M == FilterMode::HP2)
                output[i] = (u - y2) * comp;
            else if constexpr (M == FilterMode::NOTCH2)
                output[i] = (u - y2 + y4) * comp;
            else
                output[i] = y4 * comp;

            (void)y1; (void)y3;
        }
    }

    // =====================================================================
    // State
    // =====================================================================
    FilterModel model_;
    float sampleRate_;

    // 4 state variables (shared between SVF and Ladder)
    // SVF:    s1_=s1_a, s2_=s2_a, s3_=s1_b, s4_=s2_b (two 2-pole stages)
    // Ladder: s1_..s4_ are the four 1-pole integrator states
    float s1_, s2_, s3_, s4_;

    // SVF coefficients (interpolated per-block)
    float svf_a1_, svf_a2_, svf_a3_, svf_damp_;

    // Ladder coefficients (interpolated per-block)
    float ladder_g_;
    float ladder_k_;
};
