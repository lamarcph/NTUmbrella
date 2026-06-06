// =============================================================================
// StepClockOut.h — Reusable per-step clock output (50% duty gate + tick edge)
// =============================================================================
// Given a step length in samples, this generates:
//   - a 50% duty cycle gate: HIGH for the first half of the step, LOW for the
//     second half — suitable for driving a CV "clock out" bus.
//   - a "tick" pulse at the start of every step (one sample wide), suitable
//     for emitting MIDI 0xF8 timing bytes or any other per-step event.
//
// The caller owns the step length and may change it any time (BPM / clock
// source change).  This object only owns the within-step phase.  It does not
// know about audio buffers or BPM directly.
//
// Usage:
//   StepClockOut clk;
//   clk.setSamplesPerStep(sps);
//   for (int i = 0; i < n; ++i) {
//       gateBus[i] += clk.gateHigh() ? 5.0f : 0.0f;
//       if (clk.tick()) sendMidiClockByte();
//       clk.advance();
//   }
//   clk.reset();   // on transport reset
// =============================================================================
#pragma once

#include <cstdint>

struct StepClockOut {

    // Set / change the step length in samples.  Safe to call any time.
    void setSamplesPerStep(uint32_t sps) { _samplesPerStep = sps; }

    // True for the first half of the step (50% duty gate).
    bool gateHigh() const {
        if (_samplesPerStep == 0) return false;
        return _phase < (_samplesPerStep / 2);
    }

    // True for the single sample at the start of each step (rising edge).
    // Use this to emit MIDI 0xF8 or other one-shot per-step events.
    bool tick() const { return _phase == 0; }

    // Advance the phase by one sample.  Wraps to 0 at the end of the step.
    void advance() {
        if (_samplesPerStep == 0) { _phase = 0; return; }
        _phase++;
        if (_phase >= _samplesPerStep) _phase = 0;
    }

    // Snap phase to 0 (e.g. on transport reset) so the next sample is a tick.
    void reset() { _phase = 0; }

    uint32_t samplesPerStep() const { return _samplesPerStep; }
    uint32_t phase()          const { return _phase; }

private:
    uint32_t _samplesPerStep = 0;
    uint32_t _phase          = 0;
};
