// =============================================================================
// CVTrigger.h — Reusable CV rising-edge trigger detector
// =============================================================================
// Detects rising edges on a CV bus crossing a configurable threshold (default
// 1.0 V — comfortably above logic-low noise, well below a typical 5 V trigger).
// A held-high signal does NOT retrigger: a fresh low-then-high transition is
// required.  Designed to be embedded in a plugin's DTC struct.
//
// Usage:
//   CVTrigger trig;             // threshold 1.0 V by default
//   for (int i = 0; i < n; ++i) {
//       if (trig.processSample(cv[i])) doReset();
//   }
//   trig.reset();               // call when the input is disconnected
// =============================================================================
#pragma once

struct CVTrigger {

    // Process one CV sample.  Returns true if a rising edge was detected
    // (transition from below threshold to at-or-above threshold).
    bool processSample(float cv) {
        bool nowHigh = (cv >= _threshold);
        bool edge = (nowHigh && !_high);
        _high = nowHigh;
        return edge;
    }

    // Forget the latched high state so the next high sample counts as an edge.
    // Call when the input bus is disconnected/reassigned.
    void reset() { _high = false; }

    void  setThreshold(float t) { _threshold = t; }
    float getThreshold() const  { return _threshold; }
    bool  isHigh()       const  { return _high; }

private:
    float _threshold = 1.0f;
    bool  _high      = false;
};
