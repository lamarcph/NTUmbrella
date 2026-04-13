#ifndef LOFI_PARTS_LOFI_MORPH_OSCILLATOR_H
#define LOFI_PARTS_LOFI_MORPH_OSCILLATOR_H

#include <cmath>     // For M_PI, sin, fabs (for LUT generation and FM calculations)
#include <array>     // For std::array (modern C++ alternative to raw C arrays)
#include <cstdint>   // For fixed-size integer types (int16_t, uint32_t, uint64_t, int8_t)
#include <algorithm> // For std::min, std::max
#include <CheapMaths.h>

// Define PI if not available (e.g., on some Windows compilers)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- Fixed-Point Configuration ---
// Q1.15 format for output: 1 sign bit, 15 fractional bits.
// Range: approximately -1.0 to +0.9999.
// 1.0 is represented by 32767 (0x7FFF). -1.0 by -32768 (0x8000).
const int16_t Q15_MAX_VAL = 32767;
const int16_t Q15_MIN_VAL = -32768;

// Phase accumulator uses more precision to avoid quantization noise.
// We'll use 28 fractional bits for the phase. This means a full cycle (1.0)
// is represented by (1U << PHASE_FRAC_BITS).
// The phase accumulator itself will be a uint32_t.
const uint32_t PHASE_FRAC_BITS = 28;
const uint32_t PHASE_SCALE = (1U << PHASE_FRAC_BITS); // Represents 1.0 (a full cycle) in phase units

const float Q16_SCALE_FLOAT = 65536.0f;
const uint32_t Q16_SCALE_INT = 65536;
const int32_t ONE_Q15 = Q15_MAX_VAL; // 1.0 in Q1.15
const int32_t DET_STEP_Q15 = static_cast<int32_t>(0.1f * Q15_MAX_VAL + 0.5f); // 0.1 -> Q15

// --- Sine Lookup Table Configuration ---
// Size of the sine lookup table. Must be a power of 2 for efficient indexing.
const uint32_t SINE_LUT_SIZE = 1024; // 1024 entries
// Number of bits to shift the phase accumulator to get the LUT index.
// PHASE_FRAC_BITS (28) - log2(SINE_LUT_SIZE) (10) = 18
const uint32_t SINE_LUT_SHIFT = PHASE_FRAC_BITS - static_cast<uint32_t>(std::log2(SINE_LUT_SIZE));

// Static array for sine lookup table.
// This will be initialized once.
static int16_t sine_lut[SINE_LUT_SIZE];

// Function to initialize the sine LUT. Call this once at application startup.
static inline void initializeSineLUT() {
    for (uint32_t i = 0; i < SINE_LUT_SIZE; ++i) {
        // Calculate the angle for this LUT entry (0 to 2*PI)
        float angle = (static_cast<float>(i) / SINE_LUT_SIZE) * 2.0f * static_cast<float>(M_PI);
        // Calculate sine, scale to Q15, and store.
        sine_lut[i] = static_cast<int16_t>(std::sin(angle) * Q15_MAX_VAL);
    }
}

// --- Block Size Configuration ---
// Define the static size for internal buffers.
// This is critical for avoiding dynamic allocations.
const uint32_t MAX_BLOCK_SIZE = 128; // The user-requested static block size

// A simple class to generate basic aliased waveforms using fixed-point arithmetic
class OscillatorFixedPoint {
public:
    // Enum to select waveform type for getWave()
    enum WaveformType {
        SINE,
        TRIANGLE,
        SQUARE,
        SAW,
        MORPHED,
        POLYBLEP_SAW,
        POLYBLEP_SQUARE,
        WAVETABLE,
        NOISE
    };

    // Constructor initializes the oscillator
    OscillatorFixedPoint() :
        _frequency(440.0f),      // Default frequency (A4)
        _sampleRate(44100.0f),   // Default sample rate
        _phase(0),              // Current phase accumulator (fixed-point)
        _basePhaseIncrement(0), // Base phase increment without FM
        _fmDepthQ16(0),
        _fmDepth(0.0f),
        _phaseScaleDivSampleRateQ16(0),         // Default FM depth
        _morphModDepth(1.0f),   // Default morph modulation depth (1.0 means full range)
        _userShapeMorph(0)
    {
        // Ensure the sine LUT is initialized before any oscillator is used.
        // This check ensures it's only called once.
        static bool lut_initialized = false;
        if (!lut_initialized) {
            initializeSineLUT();
            lut_initialized = true;
        }
        updateBasePhaseIncrement(); // Calculate initial base phase increment
          // Pre-calculate the fixed-point constant: (Phase Scale / Sample Rate) in Q16.16 format
        // This is the multiplier needed to convert Q16.16 frequency to fixed-point phase increment.
        _phaseScaleDivSampleRateQ16 = static_cast<uint32_t>(
            (static_cast<float>(PHASE_SCALE) / _sampleRate) * Q16_SCALE_FLOAT
        );
    }

    // Set the oscillator's frequency
    void setFrequency(float freq) {
        _frequency = freq;
        updateBasePhaseIncrement();
    }

    // Set the audio sample rate
    void setSampleRate(float sr) {
        _sampleRate = sr;
         _phaseScaleDivSampleRateQ16 = static_cast<uint32_t>(
            (static_cast<float>(PHASE_SCALE) / _sampleRate) * Q16_SCALE_FLOAT
        );
        updateBasePhaseIncrement();
    }

    // Set sample-rate decimation factor (1 = off, 2 = half rate, 4 = quarter, etc.)
    // The oscillator advances phase by N steps at once, computes one sample,
    // and holds it for the remaining N-1 output slots.
    void setDecimation(uint32_t factor) {
        _decimation = (factor < 1) ? 1 : factor;
    }

     // Getter for testing the current internal phase
    uint32_t getPhase() const {
        return _phase;
    }

     // Getter for testing the base phase increment
    uint32_t getBasePhaseIncrement() const {
        return _basePhaseIncrement;
    }

    // Set the shape morphing parameter (0.0 to 1.0, where 0.0 is Sine, 1.0 is Saw)
    // This is for direct setting, not modulated.
    void setShapeMorph(float morphValue) {
        // Clamp input to 0.0-1.0 range
        if (morphValue < 0.0f) morphValue = 0.0f;
        if (morphValue > 1.0f) morphValue = 1.0f;
        // Convert floating-point morph value to Q15 fixed-point
        _userShapeMorph = static_cast<uint16_t>(morphValue * Q15_MAX_VAL);
        _shapeMorph = _userShapeMorph;
        _currentBlockMorphValues.fill(_userShapeMorph);
    }

    // Set wavetable data pointer (must point to flat full-size table, mipmaps skipped by caller)
    void setWavetable(const int16_t* data, uint32_t numWaves, uint32_t waveLength) {
        _wavetableData = data;
        _wtNumWaves = numWaves;
        _wtWaveLength = waveLength;
        _wtWavePosScale = (numWaves > 1)
            ? static_cast<float>(numWaves - 1) / static_cast<float>(Q15_MAX_VAL)
            : 0.0f;
    }

    // Set the frequency modulation depth (e.g., in Hz deviation for a 1.0 FM input)
    void setFmDepth(float depth) {
        _fmDepthQ16 = static_cast<int32_t>(depth * Q16_SCALE_FLOAT);
        _fmDepth = depth;
        
                // Get the base Q16.16 frequency (used if V/Oct is not prepared)
        
    }

    // Set the morph modulation depth (0.0 to 1.0, controlling the range of morphing)
    void setMorphModDepth(float depth) {
        // Clamp depth to a reasonable range, e.g., 0.0 to 1.0
        if (depth < 0.0f) depth = 0.0f;
        if (depth > 1.0f) depth = 1.0f;
        _morphModDepth = depth;
    }

    /**
     * @brief Prepares the per-sample base frequencies based on a V/Oct input buffer.
     * @param vOctInput Buffer of float (1.0f = one octave up).
     * @param numSamples Number of samples in the block.
     */
    void prepareVOctBlock(const float* vOctInput, uint32_t numSamples, bool usefminput) {
        _useVOctBuffer = true;       
                
        for (uint32_t i = 0; i < numSamples; ++i) {
                        
            // 2. V/Oct scaling: F_inst = F_base * 2^(smoothed_voct)
            float instant_freq_hz = _frequency * fast_powf(2.0f, vOctInput[i]);
            
            // 3. Convert to Q16.16 (65536.0f is 2^16) and store
            _currentBlockVOctFrequenciesQ16[i] = static_cast<uint32_t>(instant_freq_hz * Q16_SCALE_FLOAT);
            if (!usefminput){
                int32_t phaseInc = static_cast<uint32_t>((instant_freq_hz  * PHASE_SCALE) / _sampleRate);
                _currentBlockPhaseIncrements[i] = static_cast<uint32_t>(phaseInc);
            }
            if (debugvalue2ptr!= nullptr) *debugvalue2ptr= instant_freq_hz;
            //if (debugvalue3ptr!= nullptr) *debugvalue3ptr= static_cast<float>(_currentBlockVOctFrequenciesQ16[i]) / Q16_SCALE_FLOAT;
            //if (debugvalue4ptr!= nullptr) *debugvalue4ptr= _currentBlockPhaseIncrements[i] * 1.f;
            /*std::cout << "VOct Input: " << vOctInput[i] << ", Smoothed: " << _voctFilterState 
                      << ", Freq Hz: " << instant_freq_hz 
                      << ", Freq Q16: " << _currentBlockVOctFrequenciesQ16[i] 
                      << ", Phase Inc: " << _currentBlockPhaseIncrements[i] << std::endl;*/
        }
    }


    // --- MODIFIED: Combined V/Oct, FM, and Phase Increment Calculation ---
    /**
     * @brief Prepares phase increments and TZFM signs, combining V/Oct-modulated frequency and FM.
     * @param fmInput FM modulation values in Q1.15 format.
     * @param numSamples Number of samples in the block.
     * * NOTE: prepareVOctBlock must be called before this if V/Oct modulation is active.
     */
    void prepareFmBlock(const int16_t* fmInput, uint32_t numSamples) {
        
                // Get the base Q16.16 frequency (used if V/Oct is not prepared)
        uint32_t defaultBaseFreqQ16 = static_cast<uint32_t>(_frequency * Q16_SCALE_FLOAT);

        if (debugvalueptr!= nullptr) *debugvalueptr= 1.f;        
        for (uint32_t i = 0; i < numSamples; ++i) {
            // 1. Get V/Oct adjusted base frequency (Q16.16)
            uint32_t vOctFreqQ16 = _useVOctBuffer ? 
                                   _currentBlockVOctFrequenciesQ16[i] : 
                                   defaultBaseFreqQ16;

            // 2. Calculate FM offset in Q16.16
            // When fmInput is nullptr (fast path, no FM routing), offset is zero.
            // Calculation: (fmInput[i] (Q1.15) * fmDepthQ16 (Q16.16)) >> 15
            // The multiplication results in Q17.31. Shifting >> 15 yields Q17.16 (sufficient for addition).
           int32_t fmOffsetQ16 = fmInput ? static_cast<int32_t>((static_cast<int64_t>(fmInput[i]) * _fmDepthQ16) >> 15) : 0;

            // 3. Instantaneous frequency in Q16.16: V/Oct Freq + FM Offset
            int32_t instFreqQ16 = static_cast<int32_t>(vOctFreqQ16) + fmOffsetQ16;

            // 5. Use absolute frequency magnitude (Q16.16) for increment calculation
            uint32_t absFreqQ16 = static_cast<uint32_t>(instFreqQ16 < 0 ? -instFreqQ16 : instFreqQ16);

            // 6. Calculate phase increment: (absFreqQ16 (Q16.16) * (Phase Scale / SR) (Q16.16)) >> 16
            // The multiplication results in Q32.32. Shifting >> 16 yields Q32.16.
            // The final result is the desired Q16 phase increment (PHASE_SCALE is 2^28, which is Q4.28 format).
            // Since we discard the 16 fractional bits, the result is in Q32 format (integer portion).
            // This is the correct phase increment for a PHASE_SCALE=2^28 accumulator.
            int32_t phaseInc = (static_cast<uint64_t>(absFreqQ16) * _phaseScaleDivSampleRateQ16) >> 32;
            _currentBlockPhaseIncrements[i] = static_cast<uint32_t>(phaseInc);
            /*std::cout << "Sample " << i << ": V/Oct Freq Q16 = " << vOctFreqQ16 
                      << ", FM Offset Q16 = " << fmOffsetQ16 
                      << ", Inst Freq Q16 = " << instFreqQ16 
                      << ", Phase Inc = " << phaseInc 
                      << ", TZFM Sign = " << static_cast<int>(_currentBlockFmSigns[i]) << std::endl;
            std::cout << "  (Freq Hz = " << (static_cast<float>(instFreqQ16) / Q16_SCALE_FLOAT) << ")" << std::endl;*/
            /*if (debugvalue2ptr!= nullptr) *debugvalue2ptr= fmInput[i]/static_cast<float>(Q15_MAX_VAL);
            if (debugvalue3ptr!= nullptr) *debugvalue4ptr= static_cast<float>(vOctFreqQ16) / Q16_SCALE_FLOAT;
            if (debugvalue4ptr!= nullptr) *debugvalue4ptr= static_cast<float>(instFreqQ16) / Q16_SCALE_FLOAT;*/

        }
       _useVOctBuffer = false; // Reset flag after use
    }


    // Prepare the internal morph values for the current block.
    // This should be called once per block, alongside prepareFmBlock if needed.
    // The morphInput buffer contains modulation values in Q1.15 fixed-point format (-Q15_MAX_VAL to Q15_MAX_VAL).
    void prepareMorphBlock(const int16_t* morphInput, uint32_t numSamples) {
        // Clamp numSamples to MAX_BLOCK_SIZE to prevent buffer overflow
        // morphModDepth is a float in [0,1], convert to Q15 for fixed-point math
        uint16_t morphModDepthQ15 = static_cast<uint16_t>(_morphModDepth * Q15_MAX_VAL);

        for (uint32_t i = 0; i < numSamples; ++i) {
            // morphInput[i] is Q1.15, range -32768..32767. we want negatives values to reduce the user shape morph
            uint16_t morph = static_cast<uint16_t>(morphInput[i]);

            // Scale by morphModDepthQ15 (Q15 * Q15 >> 15 = Q15)
            uint32_t scaled = (static_cast<uint32_t>(morph) * morphModDepthQ15) >> 15;
            scaled += _userShapeMorph;

            // Clamp to Q15_MAX_VAL
            if (scaled > Q15_MAX_VAL) scaled = Q15_MAX_VAL;
            _currentBlockMorphValues[i] = static_cast<uint16_t>(scaled);
        }
    }


    // Get the current sample for a Saw wave (output in Q1.15 fixed-point)
    // A saw wave goes from -1 to 1.
    // In our phase (0 to PHASE_SCALE - 1), it's a linear ramp.
    int16_t getSawWave() {
        // Scale the phase (0 to PHASE_SCALE) to the Q15 output range (-Q15_MAX_VAL to Q15_MAX_VAL).
        // (phase / PHASE_SCALE) gives a normalized phase (0 to approx 1.0)
        // Multiply by (2 * Q15_MAX_VAL) to get a range of 0 to 2*Q15_MAX_VAL
        // Then subtract Q15_MAX_VAL to shift to -Q15_MAX_VAL to Q15_MAX_VAL.
        
        // Use uint64_t for intermediate multiplication to prevent overflow before division.
        int32_t val = static_cast<int32_t>( ( (uint64_t)_phase * (2LL * Q15_MAX_VAL) ) / PHASE_SCALE );
        return static_cast<int16_t>(val - Q15_MAX_VAL);
    }

    // Noise: xorshift32 PRNG — phase-independent, full Q15 range
    int16_t getNoiseWave() {
        _noiseState ^= _noiseState << 13;
        _noiseState ^= _noiseState >> 17;
        _noiseState ^= _noiseState << 5;
        return static_cast<int16_t>(_noiseState >> 16);
    }

    // Get the current sample for a Triangle wave (output in Q1.15 fixed-point)
    // A triangle wave goes from -1 to 1, peaking at the midpoint.
    int16_t getTriangleWave() {
        uint32_t p_folded = _phase;

        // Fold the phase:
        // If phase is in the second half of the cycle, mirror it.
        // This effectively maps 0 -> peak -> 0 over the first half of the cycle.
        if (p_folded > (PHASE_SCALE / 2)) {
            p_folded = PHASE_SCALE - p_folded;
        }

        // Now p_folded goes from 0 to PHASE_SCALE / 2.
        // Scale p_folded to the Q15 range, then multiply by 4 (for triangle slope)
        // and divide by PHASE_SCALE to normalize to 0 to 2*Q15_MAX_VAL.
        // Then subtract Q15_MAX_VAL to shift to -Q15_MAX_VAL to Q15_MAX_VAL.
        int32_t val = static_cast<int32_t>( ( (uint64_t)p_folded * (4LL * Q15_MAX_VAL) ) / PHASE_SCALE );
        return static_cast<int16_t>(val - Q15_MAX_VAL);
    }

    // Get the current sample for a Square wave (output in Q1.15 fixed-point)
    // Uses pulse width (duty cycle): 0.0 = very narrow, 0.5 = 50%, 1.0 = very wide
    int16_t getSquareWave() {
        return (_phase < _pulseWidthThreshold) ? Q15_MAX_VAL : Q15_MIN_VAL;
    }

    // Set pulse width (duty cycle) for the square wave: 0.05 to 0.95
    void setPulseWidth(float pw) {
        pw = std::clamp(pw, 0.05f, 0.95f);
        _pulseWidthThreshold = static_cast<uint32_t>(static_cast<float>(PHASE_SCALE) * pw);
    }

    float getPulseWidth() const {
        return static_cast<float>(_pulseWidthThreshold) / static_cast<float>(PHASE_SCALE);
    }

    // Get the current sample for a Sine wave (output in Q1.15 fixed-point)
    // Uses a lookup table for efficiency.
    int16_t getSineWave() {
        // Get the index into the sine lookup table.
        // Shift right by SINE_LUT_SHIFT to map the high-precision phase
        // (0 to PHASE_SCALE-1) to the LUT index range (0 to SINE_LUT_SIZE-1).
        uint32_t index = _phase >> SINE_LUT_SHIFT;
        return sine_lut[index];
    }

    // Get the current sample with smooth morphing between Sine, Triangle, Square, Saw
    // The morphing order is Sine -> Triangle -> Square -> Saw
    int16_t getMorphedWave() {
        // Define segment width for morphing (Q15_MAX_VAL / 4)
        // 4 segments: Sine → Triangle → Square → Saw → Noise
        const uint16_t SEGMENT_WIDTH = Q15_MAX_VAL / 4;

        int16_t wave_a, wave_b;
        uint16_t local_morph_param; // Fixed-point parameter within the current segment (0 to SEGMENT_WIDTH)
       
        if (_shapeMorph < SEGMENT_WIDTH) { // Segment 1: Sine to Triangle
            wave_a = getSineWave();
            wave_b = getTriangleWave();
            local_morph_param = _shapeMorph;
        } else if (_shapeMorph < (2 * SEGMENT_WIDTH)) { // Segment 2: Triangle to Square
            wave_a = getTriangleWave();
            wave_b = getSquareWave();
            local_morph_param = _shapeMorph - SEGMENT_WIDTH;
        } else if (_shapeMorph < (3 * SEGMENT_WIDTH)) { // Segment 3: Square to Saw
            wave_a = getSquareWave();
            wave_b = getSawWave();
            local_morph_param = _shapeMorph - (2 * SEGMENT_WIDTH);
        } else { // Segment 4: Saw to Noise
            wave_a = getSawWave();
            wave_b = getNoiseWave();
            local_morph_param = _shapeMorph - (3 * SEGMENT_WIDTH);
        }

        // Calculate alpha for interpolation (0 to Q15_MAX_VAL)
        // alpha = (local_morph_param / SEGMENT_WIDTH) * Q15_MAX_VAL
        // Use uint32_t for intermediate product to prevent overflow
        uint16_t alpha = static_cast<uint16_t>( ( (uint32_t)local_morph_param * Q15_MAX_VAL ) / SEGMENT_WIDTH );

        // Fixed-point linear interpolation: result = (wave_a * (1 - alpha) + wave_b * alpha)
        // (wave_a * (Q15_MAX_VAL - alpha) + wave_b * alpha) >> 15
        int32_t interpolated_sample = ( ( (int32_t)wave_a * (Q15_MAX_VAL - alpha) ) >> 15 ) +
                                      ( ( (int32_t)wave_b * alpha ) >> 15 );

        // The result should naturally stay within Q15 range due to interpolation of Q15 values.
        return static_cast<int16_t>(interpolated_sample);
    }

    // --- Single-Sample Processing Methods ---

    // Get the next sample for a specific waveform type, advancing the phase and applying FM.
    // This method is for single-sample processing where FM input is provided per call.
    int16_t getWave(WaveformType type, int16_t fmInputSample = 0) {
        // Convert the int16_t FM input (Q1.15) to float
        float float_fm_value = static_cast<float>(fmInputSample) / Q15_MAX_VAL;

        // Calculate instantaneous frequency: base frequency + (FM input * FM depth)
        float instantaneous_freq_hz = _frequency + (float_fm_value * _fmDepth);

           // NOTE: For single sample, we simply allow negative increments for consistency
        int32_t current_phase_increment = static_cast<int32_t>((instantaneous_freq_hz * PHASE_SCALE) / _sampleRate);

        // Advance phase
        _phase += current_phase_increment;
        _phase &= (PHASE_SCALE - 1); // Wrap phase (0 to PHASE_SCALE - 1)

        int16_t output_sample;
        switch (type) {
            case SINE:
                output_sample = getSineWave();
                break;
            case TRIANGLE:
                output_sample = getTriangleWave();
                break;
            case SQUARE:
                output_sample = getSquareWave();
                break;
            case SAW:
                output_sample = getSawWave();
                break;
            case MORPHED:
                output_sample = getMorphedWave();
                break;
            case WAVETABLE:
                output_sample = getWavetableSample();
                break;
            case NOISE:
                output_sample = getNoiseWave();
                break;
            default:
                output_sample = 0; // Default to silence
        }
        return output_sample; // Apply FM sign
    }


    // --- Block Processing Methods ---
    // Each block method now uses the pre-calculated phase increments and FM signs

    // Generate a block of Saw wave samples
    void getSawWaveBlock(int16_t* outputBuffer, uint32_t numSamples) {
        numSamples = std::min(numSamples, MAX_BLOCK_SIZE);
        for (uint32_t i = 0; i < numSamples; ++i) {
            _phase += _currentBlockPhaseIncrements[i];
            _phase &= (PHASE_SCALE - 1);
            if (_decimation <= 1 || _decimationCounter == 0) {
                _decimationHeld = getSawWave();
            }
            outputBuffer[i] = _decimationHeld;
            if (_decimation > 1) {
                _decimationCounter++;
                if (_decimationCounter >= _decimation) _decimationCounter = 0;
            }
        }
    }

    // Generate a block of Triangle wave samples
    void getTriangleWaveBlock(int16_t* outputBuffer, uint32_t numSamples) {
        numSamples = std::min(numSamples, MAX_BLOCK_SIZE);
        for (uint32_t i = 0; i < numSamples; ++i) {
            _phase += _currentBlockPhaseIncrements[i];
            _phase &= (PHASE_SCALE - 1);
            if (_decimation <= 1 || _decimationCounter == 0) {
                _decimationHeld = getTriangleWave();
            }
            outputBuffer[i] = _decimationHeld;
            if (_decimation > 1) {
                _decimationCounter++;
                if (_decimationCounter >= _decimation) _decimationCounter = 0;
            }
        }
    }

    // Generate a block of Square wave samples
    void getSquareWaveBlock(int16_t* outputBuffer, uint32_t numSamples) {
        numSamples = std::min(numSamples, MAX_BLOCK_SIZE);
        for (uint32_t i = 0; i < numSamples; ++i) {
            _phase += _currentBlockPhaseIncrements[i];
            _phase &= (PHASE_SCALE - 1);
            if (_decimation <= 1 || _decimationCounter == 0) {
                _decimationHeld = getSquareWave();
            }
            outputBuffer[i] = _decimationHeld;
            if (_decimation > 1) {
                _decimationCounter++;
                if (_decimationCounter >= _decimation) _decimationCounter = 0;
            }
        }
    }

    // Generate a block of Sine wave samples
    void getSineWaveBlock(int16_t* outputBuffer, uint32_t numSamples) {
        numSamples = std::min(numSamples, MAX_BLOCK_SIZE);
        for (uint32_t i = 0; i < numSamples; ++i) {
            _phase += _currentBlockPhaseIncrements[i];
            _phase &= (PHASE_SCALE - 1);
            if (_decimation <= 1 || _decimationCounter == 0) {
                _decimationHeld = getSineWave();
            }
            outputBuffer[i] = _decimationHeld;
            if (_decimation > 1) {
                _decimationCounter++;
                if (_decimationCounter >= _decimation) _decimationCounter = 0;
            }
        }
    }

    // Generate a block of Morphed wave samples
    void getMorphedWaveBlock(int16_t* outputBuffer, uint32_t numSamples) {
        numSamples = std::min(numSamples, MAX_BLOCK_SIZE);
        for (uint32_t i = 0; i < numSamples; ++i) {
            _shapeMorph = _currentBlockMorphValues[i];
            _phase += _currentBlockPhaseIncrements[i];
            _phase &= (PHASE_SCALE - 1);
            if (_decimation <= 1 || _decimationCounter == 0) {
                _decimationHeld = getMorphedWave();
            }
            outputBuffer[i] = _decimationHeld;
            if (_decimation > 1) {
                _decimationCounter++;
                if (_decimationCounter >= _decimation) _decimationCounter = 0;
            }
        }
    }

    // Generate a block of Wavetable samples (morph selects wave position)
    void getWavetableWaveBlock(int16_t* outputBuffer, uint32_t numSamples) {
        numSamples = std::min(numSamples, MAX_BLOCK_SIZE);
        for (uint32_t i = 0; i < numSamples; ++i) {
            _shapeMorph = _currentBlockMorphValues[i];
            _phase += _currentBlockPhaseIncrements[i];
            _phase &= (PHASE_SCALE - 1);
            if (_decimation <= 1 || _decimationCounter == 0) {
                _decimationHeld = getWavetableSample();
            }
            outputBuffer[i] = _decimationHeld;
            if (_decimation > 1) {
                _decimationCounter++;
                if (_decimationCounter >= _decimation) _decimationCounter = 0;
            }
        }
    }

    // Generate a block of Noise samples (phase advances for sync, PRNG for waveform)
    void getNoiseWaveBlock(int16_t* outputBuffer, uint32_t numSamples) {
        numSamples = std::min(numSamples, MAX_BLOCK_SIZE);
        for (uint32_t i = 0; i < numSamples; ++i) {
            _phase += _currentBlockPhaseIncrements[i];
            _phase &= (PHASE_SCALE - 1);
            if (_decimation <= 1 || _decimationCounter == 0) {
                _decimationHeld = getNoiseWave();
            }
            outputBuffer[i] = _decimationHeld;
            if (_decimation > 1) {
                _decimationCounter++;
                if (_decimationCounter >= _decimation) _decimationCounter = 0;
            }
        }
    }

    // --- PolyBLEP antialiasing ---
    // Standard 2nd-order PolyBLEP: corrects BOTH the sample just after AND the
    // sample just before a discontinuity.  t = normalised phase [0,1),
    // dt = normalised phase-increment per sample.
    static inline float polyblep(float t, float dt) {
        if (t < dt) {
            // Just past the discontinuity
            float tn = t / dt;
            return tn + tn - tn * tn - 1.0f;
        } else if (t > 1.0f - dt) {
            // One sample before the next discontinuity
            float tn = (t - 1.0f) / dt;
            return tn * tn + tn + tn + 1.0f;
        }
        return 0.0f;
    }

    // Generate a block of PolyBLEP-antialiased saw wave samples
    void getPolyBlepSawWaveBlock(int16_t* outputBuffer, uint32_t numSamples) {
        numSamples = std::min(numSamples, MAX_BLOCK_SIZE);
        const float invScale = 1.0f / static_cast<float>(PHASE_SCALE);
        for (uint32_t i = 0; i < numSamples; ++i) {
            uint32_t inc = _currentBlockPhaseIncrements[i];
            _phase += inc;
            _phase &= (PHASE_SCALE - 1);

            if (_decimation <= 1 || _decimationCounter == 0) {
                float p  = static_cast<float>(_phase) * invScale;
                float dp = static_cast<float>(inc)    * invScale;
                float naiveSaw = 2.0f * p - 1.0f;
                naiveSaw -= polyblep(p, dp);
                int32_t val = static_cast<int32_t>(naiveSaw * Q15_MAX_VAL);
                _decimationHeld = static_cast<int16_t>(std::clamp(val, (int32_t)Q15_MIN_VAL, (int32_t)Q15_MAX_VAL));
            }
            outputBuffer[i] = _decimationHeld;
            if (_decimation > 1) {
                _decimationCounter++;
                if (_decimationCounter >= _decimation) _decimationCounter = 0;
            }
        }
    }

    // Generate a block of PolyBLEP-antialiased square wave samples (with pulse width)
    void getPolyBlepSquareWaveBlock(int16_t* outputBuffer, uint32_t numSamples) {
        numSamples = std::min(numSamples, MAX_BLOCK_SIZE);
        const float invScale = 1.0f / static_cast<float>(PHASE_SCALE);
        for (uint32_t i = 0; i < numSamples; ++i) {
            uint32_t inc = _currentBlockPhaseIncrements[i];
            _phase += inc;
            _phase &= (PHASE_SCALE - 1);

            if (_decimation <= 1 || _decimationCounter == 0) {
                float p  = static_cast<float>(_phase) * invScale;
                float dp = static_cast<float>(inc)    * invScale;
                float pw = static_cast<float>(_pulseWidthThreshold) * invScale;
                float naiveSquare = (p < pw) ? 1.0f : -1.0f;
                naiveSquare += polyblep(p, dp);
                float pShifted = p - pw;
                if (pShifted < 0.0f) pShifted += 1.0f;
                naiveSquare -= polyblep(pShifted, dp);
                int32_t val = static_cast<int32_t>(naiveSquare * Q15_MAX_VAL);
                _decimationHeld = static_cast<int16_t>(std::clamp(val, (int32_t)Q15_MIN_VAL, (int32_t)Q15_MAX_VAL));
            }
            outputBuffer[i] = _decimationHeld;
            if (_decimation > 1) {
                _decimationCounter++;
                if (_decimationCounter >= _decimation) _decimationCounter = 0;
            }
        }
    }

    void hardSync(){_phase = 0;}

    // Unified block render with optional sync input/output
    void getWaveBlockWithSync(int16_t* outputBuffer, bool* syncOutput, const bool* syncInput,
                              WaveformType type, uint32_t numSamples) {
        numSamples = std::min(numSamples, MAX_BLOCK_SIZE);
        for (uint32_t i = 0; i < numSamples; ++i) {
            // Sync input: reset phase if master triggered
            if (syncInput && syncInput[i]) {
                _phase = 0;
            }
            // Apply per-sample morph value
            if (type == MORPHED || type == WAVETABLE) {
                _shapeMorph = _currentBlockMorphValues[i];
            }
            // Advance phase and detect wrap for sync output
            _phase += _currentBlockPhaseIncrements[i];
            if (syncOutput) {
                syncOutput[i] = (_phase >= PHASE_SCALE); // completed a full cycle
            }
            _phase &= (PHASE_SCALE - 1);

            // Decimation: only compute a new sample on the trigger beat
            bool computeSample = (_decimation <= 1 || _decimationCounter == 0);

            // Generate sample (or hold previous)
            if (computeSample) {
              switch (type) {
                case SINE:     _decimationHeld = getSineWave(); break;
                case SQUARE:   _decimationHeld = getSquareWave(); break;
                case TRIANGLE: _decimationHeld = getTriangleWave(); break;
                case SAW:      _decimationHeld = getSawWave(); break;
                case MORPHED:  _decimationHeld = getMorphedWave(); break;
                case POLYBLEP_SAW: {
                    uint32_t inc = _currentBlockPhaseIncrements[i];
                    float invS = 1.0f / static_cast<float>(PHASE_SCALE);
                    float p  = static_cast<float>(_phase) * invS;
                    float dp = static_cast<float>(inc)    * invS;
                    float naiveSaw = 2.0f * p - 1.0f;
                    naiveSaw -= polyblep(p, dp);
                    int32_t v = static_cast<int32_t>(naiveSaw * Q15_MAX_VAL);
                    _decimationHeld = static_cast<int16_t>(std::clamp(v, (int32_t)Q15_MIN_VAL, (int32_t)Q15_MAX_VAL));
                    break;
                }
                case POLYBLEP_SQUARE: {
                    uint32_t inc = _currentBlockPhaseIncrements[i];
                    float invS = 1.0f / static_cast<float>(PHASE_SCALE);
                    float p  = static_cast<float>(_phase) * invS;
                    float dp = static_cast<float>(inc)    * invS;
                    float pw = static_cast<float>(_pulseWidthThreshold) * invS;
                    float naiveSq = (p < pw) ? 1.0f : -1.0f;
                    naiveSq += polyblep(p, dp);
                    float pShifted = p - pw;
                    if (pShifted < 0.0f) pShifted += 1.0f;
                    naiveSq -= polyblep(pShifted, dp);
                    int32_t v = static_cast<int32_t>(naiveSq * Q15_MAX_VAL);
                    _decimationHeld = static_cast<int16_t>(std::clamp(v, (int32_t)Q15_MIN_VAL, (int32_t)Q15_MAX_VAL));
                    break;
                }
                case WAVETABLE:
                    _decimationHeld = getWavetableSample();
                    break;
                case NOISE:
                    _decimationHeld = getNoiseWave();
                    break;
                default:       _decimationHeld = getSawWave(); break;
              }
            }
            outputBuffer[i] = _decimationHeld;
            if (_decimation > 1) {
                _decimationCounter++;
                if (_decimationCounter >= _decimation) _decimationCounter = 0;
            }
        }
    }

    uint16_t getUserShapeMorph() const { return _userShapeMorph; }

    void setdebugValuePointers(float* val1, float* val2, float* val3, float* val4){
        debugvalueptr = val1;
        debugvalue2ptr = val2;
        debugvalue3ptr = val3;
        debugvalue4ptr = val4;
    }
/*
    _frequency(440.0f),      // Default frequency (A4)
        _sampleRate(44100.0f),   // Default sample rate
        _phase(0),              // Current phase accumulator (fixed-point)
        _basePhaseIncrement(0), // Base phase increment without FM
        _fmDepthQ16(0),
        _fmDepth(0.0f),
        _phaseScaleDivSampleRateQ16(0),         // Default FM depth
        _morphModDepth(1.0f),   // Default morph modulation depth (1.0 means full range)
        _userShapeMorph(0)
*/
protected:
    float _frequency;      // Base oscillator frequency in Hz (float for input convenience)
    float _sampleRate;     // Audio sample rate in Hz (float for input convenience)
    uint32_t _phase;        // Current phase accumulator (fixed-point, always 0 to PHASE_SCALE - 1)
    uint32_t _basePhaseIncrement; // Base phase increment without FM (fixed-point, always positive)
    uint16_t _shapeMorph;   // Fixed-point value (Q15) for shape morphing (0 to Q15_MAX_VAL)
    uint32_t _fmDepthQ16;         
    float _fmDepth;          // Frequency modulation depth in Hz (float for input convenience)
    uint32_t _phaseScaleDivSampleRateQ16;
    float _morphModDepth;   // Depth of morph modulation (0.0 to 1.0)
    uint16_t _userShapeMorph;
    uint32_t _pulseWidthThreshold = PHASE_SCALE / 2;  // Precalculated phase threshold for square wave duty cycle

    // Wavetable: pointer to flat int16_t array (full-size table, mipmaps already skipped)
    const int16_t* _wavetableData = nullptr;
    uint32_t _wtNumWaves = 0;
    uint32_t _wtWaveLength = 0;
    float _wtWavePosScale = 0.0f;  // precomputed: (numWaves-1) / Q15_MAX_VAL

    bool _useVOctBuffer = false;
    uint32_t _decimation = 1;       // Sample-rate decimation factor (1 = off)
    uint32_t _decimationCounter = 0; // Counts samples until next compute
    int16_t _decimationHeld = 0;     // Last computed sample (held during skip)
    uint32_t _noiseState = 0x12345678; // xorshift32 PRNG state for noise waveform
    float* debugvalueptr = nullptr; 
    float* debugvalue2ptr = nullptr; 
    float* debugvalue3ptr = nullptr; 
    float* debugvalue4ptr = nullptr;

    // Buffers to store pre-calculated absolute phase increments and signs for the current block, including TZFM.
    std::array<uint32_t, MAX_BLOCK_SIZE> _currentBlockPhaseIncrements;
    
    // Buffer to store pre-calculated morph values for the current block.
    std::array<uint16_t, MAX_BLOCK_SIZE> _currentBlockMorphValues;
    std::array<uint32_t, MAX_BLOCK_SIZE> _currentBlockBaseFrequenciesQ16;
    std::array<uint32_t, MAX_BLOCK_SIZE> _currentBlockVOctFrequenciesQ16;

private:

    // Lo-fi wavetable sample lookup: nearest-neighbor within wave, linear crossfade between waves.
    // No anti-aliasing (no mipmaps) — intentional for classic PPG/Microwave XT character.
    int16_t getWavetableSample() {
        if (!_wavetableData || _wtNumWaves == 0 || _wtWaveLength == 0) return 0;

        // Wave position from morph parameter (0 .. numWaves-1)
        float wavePos = static_cast<float>(_shapeMorph) * _wtWavePosScale;
        int waveIdx = static_cast<int>(wavePos);
        if (waveIdx < 0) waveIdx = 0;
        if (waveIdx >= static_cast<int>(_wtNumWaves) - 1) waveIdx = static_cast<int>(_wtNumWaves) - 2;
        float waveFrac = wavePos - static_cast<float>(waveIdx);

        // Sample position from phase: nearest-neighbor (no interpolation = maximum lo-fi)
        uint32_t sampleIdx = static_cast<uint32_t>(
            (static_cast<uint64_t>(_phase) * _wtWaveLength) >> 27
        );
        if (sampleIdx >= _wtWaveLength) sampleIdx = _wtWaveLength - 1;

        // Linear crossfade between adjacent waves
        int32_t s0 = _wavetableData[waveIdx * _wtWaveLength + sampleIdx];
        int32_t s1 = _wavetableData[(waveIdx + 1) * _wtWaveLength + sampleIdx];
        int32_t result = s0 + static_cast<int32_t>(static_cast<float>(s1 - s0) * waveFrac);
        return static_cast<int16_t>(std::clamp(result, static_cast<int32_t>(Q15_MIN_VAL), static_cast<int32_t>(Q15_MAX_VAL)));
    }

    void updateBasePhaseIncrement() {
        _basePhaseIncrement = static_cast<uint32_t>((_frequency * PHASE_SCALE) / _sampleRate);
        //std::cout << "Base Phase Increment updated to: " << _basePhaseIncrement << std::endl;
        _currentBlockPhaseIncrements.fill(_basePhaseIncrement);
        
        // Also update the base frequency Q16 array for V/Oct default
        uint32_t freqQ16 = static_cast<uint32_t>(_frequency * 65536.0f);
        _currentBlockBaseFrequenciesQ16.fill(freqQ16);
        //if (debugvalueptr!= nullptr) *debugvalueptr= _basePhaseIncrement *1.0f;
        //std::cout << "Base Phase Increment updated to: " << _basePhaseIncrement << ", frequency: " << _frequency << " Hz" << std::endl;
    }
};

#endif // LOFI_PARTS_LOFI_MORPH_OSCILLATOR_H