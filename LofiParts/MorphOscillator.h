#include <cmath>
#include <iostream>
#include <array>
#include <cstdint>
#include <algorithm>

// Define PI if not available
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
const float TWO_PI = 2.0f * static_cast<float>(M_PI);

// --- Float Configuration ---
// All values are normalized floats in the range [-1.0f, 1.0f]
// Phase accumulator is a float in the range [0.0f, 1.0f)

// --- Sine Lookup Table Configuration (Float) ---
// Size must still be a power of 2 for efficient indexing/masking if needed, 
// but primarily for memory allocation here.
const uint32_t SINE_LUT_SIZE_FLOAT = 1024;
// Static array for float sine lookup table.
static float sine_lut_float[SINE_LUT_SIZE_FLOAT];

// Function to initialize the float sine LUT. Call this once at application startup.
void initializeSineLUTFloat() {
    for (uint32_t i = 0; i < SINE_LUT_SIZE_FLOAT; ++i) {
        // Calculate the angle for this LUT entry (0 to 2*PI)
        float angle = (static_cast<float>(i) / SINE_LUT_SIZE_FLOAT) * TWO_PI;
        // Calculate sine and store (already normalized [-1.0, 1.0])
        sine_lut_float[i] = std::sin(angle);
    }
}

// --- Block Size Configuration ---
const uint32_t MAX_BLOCK_SIZE = 128; // The static block size

// A class to generate aliased waveforms using floating-point arithmetic
class OscillatorFloat {
public:
    enum WaveformType {
        SINE,
        TRIANGLE,
        SQUARE,
        SAW,
        MORPHED
    };

    OscillatorFloat() :
        _frequency(440.0f),
        _sampleRate(44100.0f),
        _phase(0.0f), // Phase is now float [0.0, 1.0)
        _basePhaseIncrement(0.0f),
        _fmDepth(0.0f),
        _morphModDepth(1.0f),
        _userShapeMorph(0.0f) // Morph is now float [0.0, 1.0]
    {
        static bool lut_initialized = false;
        if (!lut_initialized) {
            initializeSineLUTFloat();
            lut_initialized = true;
        }
        _currentBlockFmSigns.fill(1.0f);
        updateBasePhaseIncrement();
    }

    void setFrequency(float freq) {
        _frequency = freq;
        updateBasePhaseIncrement();
    }

    void setSampleRate(float sr) {
        _sampleRate = sr;
        updateBasePhaseIncrement();
    }

    void setShapeMorph(float morphValue) {
        _userShapeMorph = std::min(1.0f, std::max(0.0f, morphValue));
        _currentBlockMorphValues.fill(_userShapeMorph);
    }

    void setFmDepth(float depth) {
        _fmDepth = depth;
    }

    void setMorphModDepth(float depth) {
        _morphModDepth = std::min(1.0f, std::max(0.0f, depth));
    }

    // Input fmInput buffer is assumed to contain normalized float values [-1.0, 1.0].
    void prepareFmBlock(const float* fmInput, uint32_t numSamples) {
        numSamples = std::min(numSamples, MAX_BLOCK_SIZE);

        for (uint32_t i = 0; i < numSamples; ++i) {
            // Instantaneous frequency in Hz
            float instFreqHz = _frequency + (fmInput[i] * _fmDepth);

            // Determine sign for Through-Zero FM
            _currentBlockFmSigns[i] = (instFreqHz < 0.0f) ? -1.0f : 1.0f;

            // Use absolute frequency for phase increment
            float absFreqHz = std::fabs(instFreqHz);

            // Calculate phase increment: (absFreqHz / sampleRate) (normalized [0, 1])
            _currentBlockPhaseIncrements[i] = absFreqHz / _sampleRate;
        }
    }

    // Input morphInput buffer is assumed to contain normalized float values [-1.0, 1.0].
    void prepareMorphBlock(const float* morphInput, uint32_t numSamples) {
        numSamples = std::min(numSamples, MAX_BLOCK_SIZE);

        for (uint32_t i = 0; i < numSamples; ++i) {
            // Take absolute value of input and scale by morph depth
            float scaled_mod = std::fabs(morphInput[i]) * _morphModDepth;

            // Add base user shape morph and clamp to 1.0
            float final_morph = _userShapeMorph + scaled_mod;
            _currentBlockMorphValues[i] = std::min(1.0f, final_morph);
        }
    }

    // --- Waveform Generation (Output range [-1.0f, 1.0f]) ---

    // Saw wave: linear ramp from -1.0 to 1.0
    float getSawWave() {
        // Phase is [0.0, 1.0). (2 * phase) is [0.0, 2.0). Subtracting 1.0 gives [-1.0, 1.0).
        return (2.0f * _phase) - 1.0f;
    }

    // Triangle wave: goes from -1.0 to 1.0, peaking at 0.5 phase
    float getTriangleWave() {
        // (2 * phase) is [0.0, 2.0). Subtracting 1.0 gives [-1.0, 1.0).
        // The absolute value of this results in a ramp up from 0 to 1.0 and down to 0 over the cycle.
        // Multiplying by 2.0 gives [0.0, 2.0]. Subtracting 1.0 gives [-1.0, 1.0].
        return 2.0f * std::fabs((2.0f * _phase) - 1.0f) - 1.0f;
    }

    // Square wave: -1.0 or 1.0
    float getSquareWave() {
        return (_phase < 0.5f) ? 1.0f : -1.0f;
    }

    // Sine wave: uses a lookup table (Float)
    float getSineWave() {
        // Map phase [0.0, 1.0) to LUT index [0, SINE_LUT_SIZE_FLOAT - 1]
        uint32_t index = static_cast<uint32_t>(_phase * SINE_LUT_SIZE_FLOAT);
        // Note: Using a simple lookup, not interpolation, for analogy to the fixed-point code.
        return sine_lut_float[index];
    }

    // Morphed wave: interpolates between Sine, Triangle, Square, Saw
    float getMorphedWave(float morphValue) {
        // Define segment width (1.0 / 3)
        const float SEGMENT_WIDTH = 1.0f / 3.0f;

        float wave_a, wave_b;
        float alpha; // Interpolation factor (0.0 to 1.0 within the segment)

        if (morphValue < SEGMENT_WIDTH) { // Segment 1: Sine to Triangle
            wave_a = getSineWave();
            wave_b = getTriangleWave();
            alpha = morphValue / SEGMENT_WIDTH;
        } else if (morphValue < (2.0f * SEGMENT_WIDTH)) { // Segment 2: Triangle to Square
            wave_a = getTriangleWave();
            wave_b = getSquareWave();
            alpha = (morphValue - SEGMENT_WIDTH) / SEGMENT_WIDTH;
        } else { // Segment 3: Square to Saw (Clamped range)
            wave_a = getSquareWave();
            wave_b = getSawWave();
            alpha = (morphValue - (2.0f * SEGMENT_WIDTH)) / SEGMENT_WIDTH;
            // Clamp alpha just in case of float precision errors near 1.0
            alpha = std::min(1.0f, alpha); 
        }

        // Linear interpolation: result = (1 - alpha) * wave_a + alpha * wave_b
        return (wave_a * (1.0f - alpha)) + (wave_b * alpha);
    }


    // --- Block Processing Methods ---
    // All methods use the pre-calculated phase increments and FM signs
    // The output buffer is now an array of floats.

    // Generate a block of Saw wave samples
    void getSawWaveBlock(float* outputBuffer, uint32_t numSamples) {
        numSamples = std::min(numSamples, MAX_BLOCK_SIZE);
        for (uint32_t i = 0; i < numSamples; ++i) {
            _phase += _currentBlockPhaseIncrements[i]; // Use pre-calculated FM-adjusted absolute increment
            if (_phase >= 1.0f) _phase -= 1.0f; // Wrap phase [0.0, 1.0)
            outputBuffer[i] = getSawWave() * _currentBlockFmSigns[i]; // Apply FM sign
        }
    }

    // Generate a block of Triangle wave samples
    void getTriangleWaveBlock(float* outputBuffer, uint32_t numSamples) {
        numSamples = std::min(numSamples, MAX_BLOCK_SIZE);
        for (uint32_t i = 0; i < numSamples; ++i) {
            _phase += _currentBlockPhaseIncrements[i];
            if (_phase >= 1.0f) _phase -= 1.0f;
            outputBuffer[i] = getTriangleWave() * _currentBlockFmSigns[i];
        }
    }

    // Generate a block of Square wave samples
    void getSquareWaveBlock(float* outputBuffer, uint32_t numSamples) {
        numSamples = std::min(numSamples, MAX_BLOCK_SIZE);
        for (uint32_t i = 0; i < numSamples; ++i) {
            _phase += _currentBlockPhaseIncrements[i];
            if (_phase >= 1.0f) _phase -= 1.0f;
            outputBuffer[i] = getSquareWave() * _currentBlockFmSigns[i];
        }
    }

    // Generate a block of Sine wave samples
    void getSineWaveBlock(float* outputBuffer, uint32_t numSamples) {
        numSamples = std::min(numSamples, MAX_BLOCK_SIZE);
        for (uint32_t i = 0; i < numSamples; ++i) {
            _phase += _currentBlockPhaseIncrements[i];
            if (_phase >= 1.0f) _phase -= 1.0f;
            outputBuffer[i] = getSineWave() * _currentBlockFmSigns[i];
        }
    }

    // Generate a block of Morphed wave samples
    void getMorphedWaveBlock(float* outputBuffer, uint32_t numSamples) {
        numSamples = std::min(numSamples, MAX_BLOCK_SIZE);
        for (uint32_t i = 0; i < numSamples; ++i) {
            // Apply per-sample morph value from the prepared buffer
            float current_morph = _currentBlockMorphValues[i];

            // Apply per-sample frequency modulation
            _phase += _currentBlockPhaseIncrements[i];
            if (_phase >= 1.0f) _phase -= 1.0f;

            // Get the morphed sample and apply FM sign
            outputBuffer[i] = getMorphedWave(current_morph) * _currentBlockFmSigns[i];
        }
    }

    void hardSync(){_phase = 0.0f;}

private:
    float _frequency;
    float _sampleRate;
    float _phase; // Current phase accumulator (float, 0.0 to 1.0)
    float _basePhaseIncrement;
    float _userShapeMorph; // Float value [0.0, 1.0] for shape morphing
    float _fmDepth;
    float _morphModDepth;

    // Buffers now store float values
    std::array<float, MAX_BLOCK_SIZE> _currentBlockPhaseIncrements;
    std::array<float, MAX_BLOCK_SIZE> _currentBlockFmSigns; // +1.0f or -1.0f
    std::array<float, MAX_BLOCK_SIZE> _currentBlockMorphValues; // [0.0, 1.0]

    // Calculate the float base phase increment (frequency / sampleRate)
    void updateBasePhaseIncrement() {
        if (_sampleRate > 0.0f) {
            _basePhaseIncrement = _frequency / _sampleRate;
        } else {
            _basePhaseIncrement = 0.0f;
        }
        _currentBlockPhaseIncrements.fill(_basePhaseIncrement);
    }
};