// =============================================================================
// CVClockTracker.h — Reusable CV clock (rising-edge) BPM tracker
// =============================================================================
// Measures periods between rising edges on a CV bus to derive BPM.
// Assumes 1 PPQN (one pulse per quarter note). Designed to be embedded
// in a plugin's DTC struct alongside or instead of MidiClockTracker.
//
// Usage:
//   - Call processSample(cv) for every audio sample
//   - Query getBPM(), isActive() as needed
//   - Call setThreshold() if 0.5V default is unsuitable
//   - Call reset() to clear state (e.g. on stop)
// =============================================================================
#pragma once

#include <cstdint>

struct CVClockTracker {

    // Process one CV sample. Returns true if BPM was updated on this sample.
    bool processSample(float cv, float sampleRate) {
        bool edge = (_prevValue < _threshold && cv >= _threshold);
        _prevValue = cv;
        if (edge) {
            if (_active && _samplesSinceEdge > 0) {
                float periodSec = static_cast<float>(_samplesSinceEdge) / sampleRate;
                _bpm = 60.0f / periodSec;
                _samplesSinceEdge = 0;
                return true;
            }
            _active = true;
            _samplesSinceEdge = 0;
        }
        _samplesSinceEdge++;
        return false;
    }

    void reset() {
        _prevValue        = 0.0f;
        _samplesSinceEdge = 0;
        _bpm              = 0.0f;
        _active           = false;
    }

    // Getters
    float getBPM()    const { return _bpm; }
    bool  isActive()  const { return _active && _bpm > 0.0f; }

    void  setThreshold(float t) { _threshold = t; }

private:
    float    _prevValue        = 0.0f;
    float    _threshold        = 0.5f;
    uint32_t _samplesSinceEdge = 0;
    float    _bpm              = 0.0f;
    bool     _active           = false;
};
