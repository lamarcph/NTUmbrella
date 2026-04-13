// =============================================================================
// AllpassDiffuser.h — 4-stage Schroeder allpass diffuser for delay feedback
// =============================================================================
// Placed inside the delay feedback loop, this progressively smears repeats
// into reverb-like wash.  1st echo = slight smear, 5th echo = full diffusion.
//
// CPU cost: 4 × (2 muls + 2 adds) = 8 muls + 8 adds per sample — less than
// one biquad section.
//
// Memory: 2118 floats per instance (~8.3 KB), allocated from DRAM pool.
// =============================================================================
#ifndef LOFI_PARTS_ALLPASS_DIFFUSER_H
#define LOFI_PARTS_ALLPASS_DIFFUSER_H

#include <cstdint>
#include <cstring>

class AllpassDiffuser {
public:
    // Prime delay lengths chosen for minimal metallic colouration
    static constexpr uint32_t STAGE_LENS[4] = { 113, 337, 571, 1097 };
    static constexpr uint32_t TOTAL_SAMPLES  = 113 + 337 + 571 + 1097; // 2118

    // Default constructor — leaves diffuser inactive (no buffer).
    AllpassDiffuser() = default;

    // Construct with a pre-allocated DRAM buffer of TOTAL_SAMPLES floats.
    // The buffer is zeroed and stage pointers are set up ready to process.
    explicit AllpassDiffuser(float* buf) : buffer(buf), coeff(0.0f) {
        uint32_t offset = 0;
        for (int s = 0; s < 4; ++s) {
            stageBuffer[s] = buffer + offset;
            stageLen[s]    = STAGE_LENS[s];
            writeIdx[s]    = 0;
            offset += STAGE_LENS[s];
        }
        std::memset(buffer, 0, TOTAL_SAMPLES * sizeof(float));
    }

    // Set diffusion amount: 0 = clean pass-through, 1 = maximum smear
    void setDiffusion(float d) {
        // Map 0–1 to allpass coefficient 0–0.7 (above ~0.75 gets unstable)
        coeff = d * 0.7f;
    }

    // Process a single sample through the 4-stage allpass cascade
    inline float process(float input) {
        if (coeff < 0.001f) return input;   // bypass when diffusion off

        float x = input;
        for (int s = 0; s < 4; ++s) {
            float* buf = stageBuffer[s];
            uint32_t len = stageLen[s];
            uint32_t wi  = writeIdx[s];

            // Read from delay line (oldest sample)
            float delayed = buf[wi];

            // Schroeder allpass (unity-gain form):
            //   output  = delayed - g * input
            //   buf[n]  = input   + g * output   ← feed back OUTPUT, not delayed
            // This gives H(z) = (z^{-M} - g)/(1 - g·z^{-M}), |H| = 1 ∀ω.
            float output = delayed - coeff * x;
            buf[wi] = x + coeff * output;

            // Advance write pointer
            writeIdx[s] = (wi + 1 < len) ? wi + 1 : 0;

            x = output;  // feed into next stage
        }
        return x;
    }

    // Bytes of DRAM needed per diffuser instance
    static constexpr uint32_t dramBytes() { return TOTAL_SAMPLES * sizeof(float); }

private:
    float* buffer = nullptr;
    float* stageBuffer[4] = {};
    uint32_t stageLen[4]  = {};
    uint32_t writeIdx[4]  = {};
    float coeff = 0.0f;           // allpass coefficient (0 to ~0.7)
};

#endif // LOFI_PARTS_ALLPASS_DIFFUSER_H
