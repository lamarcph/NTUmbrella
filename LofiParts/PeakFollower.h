#pragma once
#include <cmath>

// One-pole peak envelope follower with separate attack and release time constants.
//
// Call process() once per sample with the rectified signal magnitude (>= 0).
// The filter updates at sample-rate with exact per-sample coefficients, so the
// specified attack/release times are accurate regardless of the calling block size.
//
// Typical usage:
//   PeakFollower f;
//   f.init(48000.f);
//   f.setTimes(0.001f, 0.050f);   // 1 ms attack, 50 ms release
//   for each sample:
//     f.process(std::fabs(sample));
//   float level = f.getLevel();   // 0..1

class PeakFollower {
public:
    void init(float sampleRate) {
        fs     = sampleRate;
        level  = 0.0f;
        aAlpha = 0.0f;
        rAlpha = 0.0f;
    }

    // attackSecs / releaseSecs: time to reach 63% of a step change (tau).
    // Pass 0 for instant (alpha = 0 → level tracks input immediately).
    void setTimes(float attackSecs, float releaseSecs) {
        aAlpha = (attackSecs  < 2e-5f) ? 0.0f : expf(-1.0f / (attackSecs  * fs));
        rAlpha = (releaseSecs < 2e-5f) ? 0.0f : expf(-1.0f / (releaseSecs * fs));
    }

    // Process one sample.  mag must be >= 0 (pass std::fabs(x) for mono,
    // std::fmax(std::fabs(l), std::fabs(r)) for stereo).
    inline void process(float mag) {
        const float alpha = (mag > level) ? aAlpha : rAlpha;
        level = alpha * level + (1.0f - alpha) * mag;
        // Flush subnormals: prevents ARM Cortex-M7 FPGA performance stalls
        if (level < 1e-25f) level = 0.0f;
    }

    float getLevel() const { return level; }
    void  reset()          { level = 0.0f; }

private:
    float fs     = 48000.0f;
    float aAlpha = 0.0f;
    float rAlpha = 0.0f;
    float level  = 0.0f;
};
