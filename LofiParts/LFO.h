#ifndef LOFI_PARTS_LFO_H
#define LOFI_PARTS_LFO_H

#include "LofiMorphOscillator.h"

// --- LFO subclass for modulation, including sample-and-hold behavior ---
class LFO : public OscillatorFixedPoint {
public:
    enum Shape {
        SHAPE_SINE,
        SHAPE_TRIANGLE,
        SHAPE_SQUARE,
        SHAPE_SAW,
        SHAPE_MORPHED,
        SHAPE_SAMPLE_HOLD
    };

    LFO() : OscillatorFixedPoint(), _shape(SHAPE_SINE), _holdValue(0), _holdIntervalSamples(0), _holdCounter(0) {
        setShapeMorph(0.0f); // default is sine-like
    }

    void setShape(Shape shape) {
        _shape = shape;
        if (_shape == SHAPE_SAMPLE_HOLD) {
            _holdCounter = 0;
            _holdValue = 0;
        }
    }

    void setSampleHoldRate(float hz) {
        if (hz <= 0.0f) {
            _holdIntervalSamples = 0;
            return;
        }
        _holdIntervalSamples = static_cast<uint32_t>(std::round(_sampleRate / hz));
        if (_holdIntervalSamples < 1) _holdIntervalSamples = 1;
        _holdCounter = _holdIntervalSamples;
    }

    void setUnipolar(bool enable) { _isUnipolar = enable; }

    float getNextValue() {
        if (_shape == SHAPE_SAMPLE_HOLD) {
            if (_holdCounter == 0 || _holdIntervalSamples == 0) {
                uint16_t randVal = _generator();
                _holdValue = (static_cast<float>(randVal) / 32768.0f) - 1.0f; // -1..1
                if (_isUnipolar) _holdValue = 0.5f * (_holdValue + 1.0f);
                _holdCounter = _holdIntervalSamples;
            }
            _holdCounter--;
            return _holdValue;
        }

        // Use OscillatorFixedPoint processing path for waveforms
        int16_t sample = getWave(static_cast<WaveformType>(_shape));
        
        float out = static_cast<float>(sample) / Q15_MAX_VAL;
        if (_isUnipolar) out = 0.5f * (out + 1.0f);
        return out;
    }

    void getWaveBlock(float* outBuffer, uint32_t numSamples) {
        numSamples = std::min(numSamples, MAX_BLOCK_SIZE);
        for (uint32_t i = 0; i < numSamples; ++i) {
            if (_shape == SHAPE_SAMPLE_HOLD) {
                outBuffer[i] = getNextValue();
                continue;
            }

            _phase += _currentBlockPhaseIncrements[i];
            _phase &= (PHASE_SCALE - 1);
            int16_t sample;
            switch (_shape) {
                case SHAPE_SINE: sample = getSineWave(); break;
                case SHAPE_TRIANGLE: sample = getTriangleWave(); break;
                case SHAPE_SQUARE: sample = getSquareWave(); break;
                case SHAPE_SAW: sample = getSawWave(); break;
                case SHAPE_MORPHED: sample = getMorphedWave(); break;
                default: sample = getSineWave(); break;
            }
            float val = static_cast<float>(sample) / Q15_MAX_VAL;
            outBuffer[i] = _isUnipolar ? 0.5f * (val + 1.0f) : val;
        }
    }

private:
    Shape _shape;
    bool _isUnipolar = false;
    float _holdValue;
    uint32_t _holdIntervalSamples;
    uint32_t _holdCounter;

    uint16_t _generator() {
        return xorshift16();
    }
};

#endif // LOFI_PARTS_LFO_H
