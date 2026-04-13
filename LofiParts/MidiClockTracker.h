// =============================================================================
// MidiClockTracker.h — Reusable MIDI clock BPM tracker
// =============================================================================
// Tracks MIDI clock messages (0xF8 timing, 0xFA start, 0xFB continue, 0xFC
// stop) and derives BPM. Designed to be embedded in a plugin's DTC struct.
//
// Auto-starts on the first F8 tick even without a preceding Start/Continue
// message, so the clock locks correctly when the source was already running
// before the plugin loaded.
//
// Usage:
//   - Call advance(numSamples) at the end of each audio block
//   - Call onRealtimeByte(byte, sampleRate) from the MIDI realtime callback
//   - Query getBPM(), isActive(), quarterNoteHz() as needed
// =============================================================================
#pragma once

#include <cstdint>

struct MidiClockTracker {

    // Advance the internal sample counter. Call once per audio block.
    void advance(uint32_t numSamples) { _sampleCounter += numSamples; }

    // Process a MIDI realtime byte. Returns true if BPM was updated.
    bool onRealtimeByte(uint8_t byte, float sampleRate) {
        if (byte == 0xF8) { // Timing Clock (24 PPQN)
            // Auto-start on first F8 if not running — handles the common
            // case where the MIDI clock source was already running when the
            // plugin loaded (no Start/Continue message received).
            if (!_running) {
                _running = true;
                _ticksInGroup = 0;
                _groupStartSample = _sampleCounter;
            }
            _ticksInGroup++;
            if (_ticksInGroup >= 6) {
                uint32_t elapsed = _sampleCounter - _groupStartSample;
                if (elapsed > 0) {
                    // 6 ticks in `elapsed` samples
                    // BPM = 60 * sr * 6 / (elapsed * 24) = 15 * sr / elapsed
                    _bpm = 15.0f * sampleRate / static_cast<float>(elapsed);
                    _groupStartSample = _sampleCounter;
                    _ticksInGroup = 0;
                    return true;
                }
                _groupStartSample = _sampleCounter;
                _ticksInGroup = 0;
            }
        } else if (byte == 0xFA || byte == 0xFB) { // Start or Continue
            _running = true;
            _ticksInGroup = 0;
            _groupStartSample = _sampleCounter;
        } else if (byte == 0xFC) { // Stop
            _running = false;
            _bpm = 0.0f;
            return true;
        }
        return false;
    }

    // Getters
    float    getBPM()        const { return _bpm; }
    bool     isRunning()     const { return _running; }
    bool     isActive()      const { return _running && _bpm > 0.0f; }
    float    quarterNoteHz() const { return _bpm / 60.0f; }

private:
    uint32_t _sampleCounter     = 0;
    uint32_t _groupStartSample  = 0;
    uint8_t  _ticksInGroup      = 0;
    float    _bpm               = 0.0f;
    bool     _running           = false;
};
