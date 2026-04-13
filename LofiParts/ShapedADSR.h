#pragma once
#include <algorithm>

class ShapedADSR {
public:
    enum State { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };

    ShapedADSR() : state(IDLE), currentLevel(0.0f), targetLevel(0.0f),
                   aStep(0.0f), dStep(0.0f), rStep(0.0f), sustainLevel(0.0f),
                   attackTime(0.0f), sampleRate(44100.0f), shape(0.0f) {}

    void setSampleRate(float sr) { sampleRate = sr; }

    void setParameters(float a, float d, float s, float r) {
        // Store raw times for "Snap" logic checks
        attackTime = a;
        // Steps per sample
        aStep = (a > 0.0001f) ? 1.0f / (a * sampleRate) : 1.0f;
        dStep = (d > 0.0001f) ? 1.0f / (d * sampleRate) : 1.0f;
        rStep = (r > 0.0001f) ? 1.0f / (r * sampleRate) : 1.0f;
        sustainLevel = std::min(std::max(s, 0.0f), 1.0f);
    }

    void setShape(float s) {
        shape = std::min(std::max(s, -0.99f), 0.99f);
        _onePlusShape = 1.0f + shape;
        _shapeIsZero = (shape > -0.001f && shape < 0.001f);
    }

    void gate(bool on) {
        if (on) {
            state = ATTACK;
            // "Snap" Logic: if attack is near zero, jump to peak immediately
            if (attackTime < 0.002f) {
                currentLevel = 1.0f;
                state = DECAY;
            } else {
                currentLevel = 0.0f;
            }
        } else {
            if (state != IDLE) state = RELEASE;
        }
    }

    /**
     * advanceBlock(): Calculates the target level for the end of the block.
     * numSamples: typically 48 for your setup.
     */
    void advanceBlock(int numSamples) {
        targetLevel = currentLevel;
        
        // We simulate the FSM forward by 'numSamples' steps
        for (int i = 0; i < numSamples; ++i) {
            switch (state) {
                case ATTACK:
                    targetLevel += aStep;
                    if (targetLevel >= 1.0f) {
                        targetLevel = 1.0f;
                        state = DECAY;
                    }
                    break;
                case DECAY:
                    if (targetLevel > sustainLevel) {
                        targetLevel -= dStep;
                        if (targetLevel < sustainLevel) targetLevel = sustainLevel;
                    } else {
                        state = SUSTAIN;
                    }
                    break;
                case SUSTAIN:
                    targetLevel = sustainLevel;
                    break;
                case RELEASE:
                    targetLevel -= rStep;
                    if (targetLevel <= 0.0f) {
                        targetLevel = 0.0f;
                        state = IDLE;
                    }
                    break;
                case IDLE:
                    targetLevel = 0.0f;
                    break;
            }
        }
    }

    // Getters for the Filter/VCA interpolation
    float getCurrentLevelShaped() const { return applyShape(currentLevel); }
    float getTargetLevelShaped() const { return applyShape(targetLevel); }
    
    // Update the internal state for the next block
    void finalizeBlock() { currentLevel = targetLevel; }

    bool isActive() const { return state != IDLE || currentLevel > 0.0001f; }
    bool isGated() const { return state == ATTACK || state == DECAY || state == SUSTAIN; }

private:
    inline float applyShape(float x) const {
        // x is 0..1, shape is -0.99..0.99
        // Fast path: when shape ≈ 0, applyShape(x) ≈ x
        if (_shapeIsZero) return x;
        return (x * _onePlusShape) / (1.0f + shape * x);
    }

    State state;
    float currentLevel, targetLevel;
    float aStep, dStep, rStep, sustainLevel;
    float attackTime, sampleRate, shape;
    float _onePlusShape = 1.0f;   // pre-computed: 1.0f + shape
    bool  _shapeIsZero = true;    // true when |shape| < 0.001
};