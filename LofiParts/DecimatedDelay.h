#ifndef LOFI_PARTS_DECIMATED_DELAY_H
#define LOFI_PARTS_DECIMATED_DELAY_H

#include <cstdint>
#include <cmath>
#include <new>
#include "AllpassDiffuser.h"

#define DELAY_SIZE 65536  // ~1.48s at 44.1kHz, ~0.68s at 96kHz
#define DELAY_MASK (DELAY_SIZE - 1)

class DecimatedDelay {
public:
    float* buffer;
    uint32_t writePtr = 0;
    AllpassDiffuser diffuser;

    // Per-sample delay time smoother (prevents block-boundary clicks)
    float _smoothedDelay = 0.0f;

    // One-pole feedback filter state (§11d)
    float _fbFilterState = 0.0f;
    float _fbFilterCoeff = 0.0f;   // 0=bypass, >0=LP, <0=HP
    int   _fbFilterMode  = 0;      // 0=Off, 1=LP, 2=HP

    DecimatedDelay(float* buf) : buffer(buf) {
        // Assume buffer is pre-zeroed or handle initialization elsewhere
    }

    // Reconstruct diffuser in-place with the given DRAM buffer
    void initDiffuser(float* diffuserBuf) {
        new (&diffuser) AllpassDiffuser(diffuserBuf);
    }

    void setDiffusion(float d) {
        diffuser.setDiffusion(d);
    }

    // Set feedback filter: mode 0=Off, 1=LP, 2=HP. freqHz is cutoff.
    void setFeedbackFilter(int mode, float freqHz, float sampleRate) {
        _fbFilterMode = mode;
        if (mode == 0) {
            _fbFilterCoeff = 0.0f;
            return;
        }
        float fc = freqHz / sampleRate;
        float c = 1.0f - std::exp(-6.2831853f * fc);  // one-pole coefficient
        _fbFilterCoeff = (mode == 2) ? -c : c;  // negative = HP
    }

    // Reset smoother to a known delay value (call on noteOn)
    void resetSmoothedDelay(float d) { _smoothedDelay = d; }

    inline float process(float input, float delaySamples, float feedback, float dryWet, float* rawDelayed = nullptr) {
        // Read with linear interpolation for sub-sample accuracy
        float readPos = (float)writePtr - delaySamples;
        if (readPos < 0.0f) readPos += DELAY_SIZE;
        uint32_t i0 = ((uint32_t)readPos) & DELAY_MASK;
        uint32_t i1 = (i0 + 1) & DELAY_MASK;
        float frac = readPos - (float)(uint32_t)readPos;
        float delayed = buffer[i0] + frac * (buffer[i1] - buffer[i0]);

        if (rawDelayed) *rawDelayed = delayed;

        // Diffuse the delayed signal before feeding it back — this makes
        // each repeat progressively more smeared, dissolving echoes into wash.
        float diffused = diffuser.process(delayed);

        // Feedback filter (§11d): one-pole LP or HP in feedback path
        // LP: y = y + c*(x-y),  HP: y = x - LP(x)
        if (_fbFilterCoeff > 0.0f) {
            // LP mode
            _fbFilterState += _fbFilterCoeff * (diffused - _fbFilterState);
            diffused = _fbFilterState;
        } else if (_fbFilterCoeff < 0.0f) {
            // HP mode: complement of LP
            float c = -_fbFilterCoeff;
            _fbFilterState += c * (diffused - _fbFilterState);
            diffused = diffused - _fbFilterState;
        }
        // coeff == 0 → bypass (no branch needed in fast path)

        // Write to buffer with feedback (filtered+diffused path)
        buffer[writePtr] = input + (diffused * feedback);
        writePtr = (writePtr + 1) & DELAY_MASK;

        return (input * (1.0f - dryWet)) + (delayed * dryWet);
    }
};

#endif