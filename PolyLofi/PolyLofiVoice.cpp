#include "PolyLofiVoice.h"
#include "CheapMaths.h"

#define MAX_BLOCK_SIZE 64

// ============================================================================
// Construction & setup
// ============================================================================

PolyLofiVoice::PolyLofiVoice(float* delayBuffer) : voiceDelay(delayBuffer) {
    active = false;
    note = -1;
    velocity = 0.0f;
    noteRandom = 0.0f;

    for (int i = 0; i < NUM_OSC; ++i) {
        osc[i].setSampleRate(44100.0f);
        oscWaveform[i] = 3;
        oscSemitone[i] = 0.0f;
        oscFine[i] = 0.0f;
        oscMorph[i] = 0.0f;
        oscLevel[i] = 0.3333f;
    }

    ampEnv.setParameters(0.01f, 0.1f, 0.8f, 0.5f); // A, D, S, R
    filterEnv.setParameters(0.05f, 0.1f, 0.8f, 0.2f); // A, D, S, R

    delaySamples = 12000.0f;
    delayFeedback = 0.25f;
    delayMix = 0.25f;
}

void PolyLofiVoice::setSampleRate(float sr, float blockSize) {
    voiceSampleRate = sr;
    for (int i = 0; i < NUM_OSC; ++i) {
        osc[i].setSampleRate(sr);
        lfo[i].setSampleRate(sr/blockSize); // LFO runs at block rate for modulation
    }
    ampEnv.setSampleRate(sr);
    filterEnv.setSampleRate(sr);
    modEnv.setSampleRate(sr);
    filter.setSampleRate(sr);
}

// ============================================================================
// Parameter setters
// ============================================================================

void PolyLofiVoice::setAmpEnv(float a, float d, float s, float r) {
    baseAmpAttack = a; baseAmpDecay = d; baseAmpSustain = s; baseAmpRelease = r;
    ampEnv.setParameters(a, d, s, r);
}

void PolyLofiVoice::initDelayDiffuser(float* diffuserBuf) { voiceDelay.initDiffuser(diffuserBuf); }
void PolyLofiVoice::setDelayDiffusion(float d) { delayDiffusion = d; voiceDelay.setDiffusion(d); }

void PolyLofiVoice::setFilterEnv(float a, float d, float s, float r) {
    baseFilterAttack = a; baseFilterDecay = d; baseFilterSustain = s; baseFilterRelease = r;
    filterEnv.setParameters(a, d, s, r);
}

void PolyLofiVoice::setAmpShape(float shape) { baseAmpShape = shape; }
void PolyLofiVoice::setFilterShape(float shape) { baseFilterShape = shape; }

void PolyLofiVoice::setModEnv(float a, float d, float s, float r) {
    baseModAttack = a; baseModDecay = d; baseModSustain = s; baseModRelease = r;
    modEnv.setParameters(a, d, s, r);
}

void PolyLofiVoice::setModShape(float shape) { baseModShape = shape; modEnv.setShape(shape); }

void PolyLofiVoice::setLfoFrequency(float freq) {
    for (int i = 0; i < NUM_OSC; ++i) {
        lfo[i].setFrequency(freq);
    }
}

void PolyLofiVoice::setLfoFrequency(int lfoIdx, float freq) {
    if (lfoIdx < 0 || lfoIdx >= NUM_OSC) return;
    baseLfoFreq[lfoIdx] = freq;
    lfo[lfoIdx].setFrequency(freq);
}

void PolyLofiVoice::setLfoShape(int lfoIdx, int shape) {
    if (lfoIdx < 0 || lfoIdx >= NUM_OSC) return;
    if (shape >= 0 && shape <= 5) {
        lfo[lfoIdx].setShape(static_cast<LFO::Shape>(shape));
    }
}

void PolyLofiVoice::setLfoUnipolar(int lfoIdx, bool unipolar) {
    if (lfoIdx < 0 || lfoIdx >= NUM_OSC) return;
    lfo[lfoIdx].setUnipolar(unipolar);
}

void PolyLofiVoice::setLfoMorph(int lfoIdx, float morph) {
    if (lfoIdx < 0 || lfoIdx >= NUM_OSC) return;
    lfo[lfoIdx].setShapeMorph(morph);
}

void PolyLofiVoice::setLfoSampleHoldRate(int lfoIdx, float hz) {
    if (lfoIdx < 0 || lfoIdx >= NUM_OSC) return;
    lfo[lfoIdx].setSampleHoldRate(hz);
}

void PolyLofiVoice::setOscillatorParameters(int index, int waveform, float semitoneOffset, float fineOffset, float morph, float level) {
    if (index < 0 || index >= NUM_OSC) return;
    oscWaveform[index] = waveform;
    oscSemitone[index] = semitoneOffset;
    oscFine[index] = fineOffset;
    oscMorph[index] = morph;
    oscLevel[index] = level;
}

void PolyLofiVoice::setOscWavetable(int oscIdx, const int16_t* data, uint32_t numWaves, uint32_t waveLength) {
    if (oscIdx < 0 || oscIdx >= NUM_OSC) return;
    osc[oscIdx].setWavetable(data, numWaves, waveLength);
}

// ============================================================================
// Note lifecycle
// ============================================================================

void PolyLofiVoice::noteOn(int midiNote, float vel) {
    bool wasActive = active && (note >= 0);
    int prevNote = note;

    note = midiNote;
    velocity = vel;
    active = true;
    stealFadeCounter = 0;
    stealTailRemaining = 0;  // Clear any residual crossfade tail
    stealTailReadPos = 0;
    sustainHeldOff = false;  // Key is actively held — clear deferred note-off

    // Calculate target frequencies for new note
    for (int i = 0; i < NUM_OSC; ++i) {
        float semitoneFreq = ((float)note - 69.0f + oscSemitone[i] + pitchBendSemitones) / 12.0f;
        float freq = 440.0f * fast_powf(2.0f, semitoneFreq) * fast_powf(2.0f, oscFine[i] / 1200.0f);
        targetFreq[i] = freq;
    }

    // Generate a new random value for this note (uniform -1..1)
    noteRandom = 2.0f * ((float)rand() / (float)RAND_MAX) - 1.0f;

    // Decide whether to glide or jump
    bool shouldGlide = (glideTimeMs > 0.0f) && wasActive &&
                       (glideMode == 1 || (glideMode == 2 && prevNote != midiNote));

    if (shouldGlide) {
        // Set up glide from current freq to target
        float glideBlocks = (glideTimeMs * 0.001f * voiceSampleRate) / 128.0f;
        if (glideBlocks < 1.0f) glideBlocks = 1.0f;
        glideBlocksRemaining = static_cast<int>(glideBlocks);
        for (int i = 0; i < NUM_OSC; ++i) {
            // currentFreq[i] already holds the frequency from the previous note
            glideFreqStep[i] = (targetFreq[i] - currentFreq[i]) / glideBlocks;
        }
    } else {
        // Instant jump
        for (int i = 0; i < NUM_OSC; ++i) {
            currentFreq[i] = targetFreq[i];
            osc[i].setFrequency(currentFreq[i]);
            osc[i].hardSync();
        }
        glideBlocksRemaining = 0;
    }

    ampEnv.gate(true);
    filterEnv.gate(true);
    modEnv.gate(true);

    // Pitch-tracked comb delay (§11f): override delaySamples from note frequency
    if (delayPitchTrackMode > 0 && currentFreq[0] > 10.0f) {
        float period = voiceSampleRate / currentFreq[0];
        delaySamples = period / kPitchTrackMultipliers[delayPitchTrackMode];
        delaySamples = std::clamp(delaySamples, 1.0f, static_cast<float>(DELAY_SIZE - 1));
    }
    voiceDelay.resetSmoothedDelay(delaySamples);

    // Reset LFO phase when Key Sync is enabled
    for (int i = 0; i < 3; ++i) {
        if (lfoKeySync[i]) lfo[i].hardSync();
    }

    filter.setModel(static_cast<FilterModel>(filterModel));
    filter.reset();
}

void PolyLofiVoice::stealVoice(int midiNote, float vel) {
    renderStealTail();
    // Save tail state — noteOn() clears it as a safety reset,
    // but we need the pre-rendered tail to survive.
    int savedTailRemaining = stealTailRemaining;
    int savedTailReadPos   = stealTailReadPos;
    // Force instant (no glide) on steal
    int savedGlideMode = glideMode;
    glideMode = 0;
    noteOn(midiNote, vel);
    glideMode = savedGlideMode;
    stealFadeCounter = STEAL_FADE_SAMPLES;
    // Restore tail so processBlock() mixes the old voice fade-out
    stealTailRemaining = savedTailRemaining;
    stealTailReadPos   = savedTailReadPos;
}

// Legato retrigger: change pitch (with optional glide) without
// retriggering envelopes or resetting oscillator phase / LFOs.
void PolyLofiVoice::legatoRetrigger(int midiNote, float vel) {
    note = midiNote;
    velocity = vel;

    for (int i = 0; i < NUM_OSC; ++i) {
        float semitoneFreq = ((float)note - 69.0f + oscSemitone[i] + pitchBendSemitones) / 12.0f;
        float freq = 440.0f * fast_powf(2.0f, semitoneFreq) * fast_powf(2.0f, oscFine[i] / 1200.0f);
        targetFreq[i] = freq;
    }

    bool shouldGlide = (glideTimeMs > 0.0f) &&
                       (glideMode == 1 || glideMode == 2);
    if (shouldGlide) {
        float glideBlocks = (glideTimeMs * 0.001f * voiceSampleRate) / 128.0f;
        if (glideBlocks < 1.0f) glideBlocks = 1.0f;
        glideBlocksRemaining = static_cast<int>(glideBlocks);
        for (int i = 0; i < NUM_OSC; ++i) {
            glideFreqStep[i] = (targetFreq[i] - currentFreq[i]) / glideBlocks;
        }
    } else {
        for (int i = 0; i < NUM_OSC; ++i) {
            currentFreq[i] = targetFreq[i];
            osc[i].setFrequency(currentFreq[i]);
        }
        glideBlocksRemaining = 0;
    }
}

void PolyLofiVoice::noteOff() {
    if (sustainPedalDown) {
        sustainHeldOff = true;  // Defer the note-off until pedal is released
        return;
    }
    ampEnv.gate(false);
    filterEnv.gate(false);
    modEnv.gate(false);
}

void PolyLofiVoice::releaseSustain() {
    sustainPedalDown = false;
    if (sustainHeldOff) {
        sustainHeldOff = false;
        ampEnv.gate(false);
        filterEnv.gate(false);
        modEnv.gate(false);
    }
}

void PolyLofiVoice::setSustainPedal(bool down) {
    sustainPedalDown = down;
    if (!down) releaseSustain();
}

bool PolyLofiVoice::isAmpGated() const {
    return ampEnv.isGated();
}

void PolyLofiVoice::setPitchBend(float semitones) {
    if (pitchBendSemitones != semitones) {
        pitchBendSemitones = semitones;
        if (active && note >= 0) {
            updateOscFrequencies();
        }
    }
}

void PolyLofiVoice::setModWheel(float value) {
    modWheelValue = value;
}

void PolyLofiVoice::setAftertouch(float value) {
    aftertouchValue = value;
}

float PolyLofiVoice::getCurrentAmplitudeLevel() const {
    // A voice mid-steal-fade reports maximum level so the allocator
    // won't re-steal it before the crossfade completes.
    if (stealFadeCounter > 0) return 1.0f;
    float envLevel = ampEnv.getCurrentLevelShaped();
    // delayEnergy is mean-squared; take sqrt for comparable scale
    float delayLevel = std::sqrt(delayEnergy);
    return std::max(envLevel, delayLevel);
}

void PolyLofiVoice::setPan(float p) {
    // p in [0,1]: 0=full left, 0.5=center, 1=full right
    static constexpr float kHalfPi = 1.5707963267948966f;
    p = std::clamp(p, 0.0f, 1.0f);
    panL = std::cos(kHalfPi * p);
    panR = std::sin(kHalfPi * p);
}

// ============================================================================
// Audio processing
// ============================================================================

void PolyLofiVoice::processBlock(float* out, int numSamples, const ModSlot* matrix) {
    // Mix steal crossfade tail from previous voice (fading out)
    if (stealTailRemaining > 0) {
        int n = std::min(numSamples, stealTailRemaining);
        for (int i = 0; i < n; ++i) {
            out[i] += stealTailBuf[stealTailReadPos + i];
        }
        stealTailReadPos += n;
        stealTailRemaining -= n;
    }

    // Dead voice: nothing to do
    if (!active) return;

    bool envActive = ampEnv.isActive();

    // If envelope is finished, run delay-tail-only mode:
    // feed zeros into delay and output the decaying echoes.
    if (!envActive) {
        float delayModSamples = modOffsets[kDestDelayTime] * (kDelayModMaxMs * 0.001f * voiceSampleRate);
        float targetDelaySamples = std::clamp(delaySamples + delayModSamples, 1.0f, static_cast<float>(DELAY_SIZE - 1));
        float modulatedDelayFeedback = std::clamp(delayFeedback + modOffsets[kDestDelayFeedback], 0.0f, 0.99f);
        float modulatedDelayMix = std::clamp(delayMix + modOffsets[kDestDelayMix], 0.0f, 1.0f);
        float energy = 0.0f;
        float smoothStep = (targetDelaySamples - voiceDelay._smoothedDelay) / static_cast<float>(numSamples);
        for (int i = 0; i < numSamples; ++i) {
            voiceDelay._smoothedDelay += smoothStep;
            float raw;
            float wet = voiceDelay.process(0.0f, voiceDelay._smoothedDelay, modulatedDelayFeedback, modulatedDelayMix, &raw);
            out[i] += wet;
            energy += wet * wet;  // measure WET output (includes diffuser energy)
        }
        delayEnergy = energy / static_cast<float>(numSamples);
        // Kill voice only AFTER processing tail and measuring energy.
        // Threshold is very low (~-70 dB) because the allpass diffuser
        // spreads energy across its internal buffers — the tail stays
        // audible longer than the main delay tap alone suggests.
        if (delayEnergy < 1e-7f) {
            active = false;
            note = -1;
            stealFadeCounter = 0;
            delayEnergy = 0.0f;
        }
        return;
    }

    ampEnv.advanceBlock(numSamples);
    filterEnv.advanceBlock(numSamples);
    modEnv.advanceBlock(numSamples);

    // Apply LFO speed modulation (one-block delay from mod matrix)
    {
        float lfoSpeedMod = modOffsets[kDestLfoSpeed];
        if (std::fabs(lfoSpeedMod) > 0.001f) {
            float speedMul = fast_exp2f(lfoSpeedMod * 4.0f);
            for (int i = 0; i < 3; ++i)
                lfo[i].setFrequency(baseLfoFreq[i] * speedMul);
            lfoSpeedModActive = true;
        } else if (lfoSpeedModActive) {
            for (int i = 0; i < 3; ++i)
                lfo[i].setFrequency(baseLfoFreq[i]);
            lfoSpeedModActive = false;
        }
    }

    // LFO generation (voice-local) at control rate (single sample per block)
    float voiceLfoValue = lfo[0].getNextValue();
    float voiceLfo2Value = lfo[1].getNextValue();
    float voiceLfo3Value = lfo[2].getNextValue();

    // Calculate modulation sources
    modSources[kSourceOff] = 0.0f;
    modSources[kSourceLFO] = voiceLfoValue;
    modSources[kSourceLFO2] = voiceLfo2Value;
    modSources[kSourceLFO3] = voiceLfo3Value;
    modSources[kSourceAmpEnv] = ampEnv.getCurrentLevelShaped();
    modSources[kSourceFilterEnv] = filterEnv.getCurrentLevelShaped();
    modSources[kSourceModEnv] = modEnv.getCurrentLevelShaped();
    modSources[kSourceVelocity] = velocity;
    modSources[kSourceModWheel] = modWheelValue;
    modSources[kSourceAftertouch] = aftertouchValue;
    modSources[kSourceNoteRandom] = noteRandom;
    modSources[kSourceKeyTracking] = (note >= 0) ? (note - 60) / 60.0f : 0.0f;

    const float q15ToFloat = 1.0f / 32768.0f;

    // Initialize modulation offsets
    for (int i = 0; i < kNumDests; ++i) {
        modOffsets[i] = 0.0f;
    }

    // Accumulate modulation from all slots
    for (int slot = 0; slot < NUM_MOD_SLOTS; ++slot) {
        const ModSlot& mod = matrix[slot];
        if (mod.sourceIdx >= 0 && mod.sourceIdx < kNumSources && 
            mod.destIdx >= 0 && mod.destIdx < kNumDests) {
            modOffsets[mod.destIdx] += modSources[mod.sourceIdx] * mod.amount;
        }
    }

    // Apply envelope time modulation (fast_exp2f replaces powf(2,x) for ARM perf)
    float modAmpAttack = std::clamp(baseAmpAttack * fast_exp2f(modOffsets[kDestAmpAttack] * 4.0f), 0.001f, 10.0f);
    float modAmpDecay = std::clamp(baseAmpDecay * fast_exp2f(modOffsets[kDestAmpDecay] * 4.0f), 0.001f, 10.0f);
    float modAmpRelease = std::clamp(baseAmpRelease * fast_exp2f(modOffsets[kDestAmpRelease] * 4.0f), 0.001f, 10.0f);
    
    float modFilterAttack = std::clamp(baseFilterAttack * fast_exp2f(modOffsets[kDestFilterAttack] * 4.0f), 0.001f, 10.0f);
    float modFilterDecay = std::clamp(baseFilterDecay * fast_exp2f(modOffsets[kDestFilterDecay] * 4.0f), 0.001f, 10.0f);
    float modFilterRelease = std::clamp(baseFilterRelease * fast_exp2f(modOffsets[kDestFilterRelease] * 4.0f), 0.001f, 10.0f);

    // Update envelope parameters with modulation
    ampEnv.setParameters(modAmpAttack, modAmpDecay, baseAmpSustain, modAmpRelease);
    filterEnv.setParameters(modFilterAttack, modFilterDecay, baseFilterSustain, modFilterRelease);

    float voiceBuffer[MAX_BLOCK_SIZE];
    for (int i = 0; i < numSamples; ++i) {
        voiceBuffer[i] = 0.0f;
    }

    // Advance glide: interpolate frequencies per block
    if (glideBlocksRemaining > 0) {
        for (int i = 0; i < NUM_OSC; ++i) {
            currentFreq[i] += glideFreqStep[i];
            osc[i].setFrequency(currentFreq[i]);
        }
        glideBlocksRemaining--;
        if (glideBlocksRemaining == 0) {
            // Snap to target
            for (int i = 0; i < NUM_OSC; ++i) {
                currentFreq[i] = targetFreq[i];
                osc[i].setFrequency(currentFreq[i]);
            }
        }
    }

    // Apply pitch modulation from mod matrix (±12 semitones at full depth)
    // plus direct LFO2→Vibrato (in cents)
    {
        float pitchModSemitones = modOffsets[kDestPitch] * 12.0f;

        // Direct LFO2→Vibrato: lfo2VibratoMod is 0-100 cents depth
        if (lfo2VibratoMod > 0.001f) {
            pitchModSemitones += voiceLfo2Value * (lfo2VibratoMod / 100.0f);
        }

        float pitchMul = 1.0f;
        
        if (std::fabs(pitchModSemitones) > 0.001f) {
            pitchMul = fast_exp2f(pitchModSemitones / 12.0f);
        }
        
        // Apply per-oscillator pitch modulation on top of global pitch
        for (int i = 0; i < NUM_OSC; ++i) {
            float oscPitchMod = (i == 0) ? modOffsets[kDestOsc1Pitch] :
                               (i == 1) ? modOffsets[kDestOsc2Pitch] :
                                         modOffsets[kDestOsc3Pitch];
            float oscPitchModSemitones = oscPitchMod * 12.0f;
            float oscPitchMul = pitchMul;
            
            if (std::fabs(oscPitchModSemitones) > 0.001f) {
                oscPitchMul *= fast_exp2f(oscPitchModSemitones / 12.0f);
            }
            
            if (std::fabs(pitchModSemitones) > 0.001f || std::fabs(oscPitchModSemitones) > 0.001f) {
                osc[i].setFrequency(currentFreq[i] * oscPitchMul);
            }
        }
    }

    // Per-osc level modulation is applied inline in each rendering path

    // Check if any FM or sync routing is active (including mod matrix offsets)
    bool anyFmOrSync = syncEnable3to2 || syncEnable3to1 || syncEnable2to1
        || fmDepth3to2 > 0.0f || fmDepth3to1 > 0.0f || fmDepth2to1 > 0.0f
        || modOffsets[kDestFM3to2] > 0.0f || modOffsets[kDestFM3to1] > 0.0f || modOffsets[kDestFM2to1] > 0.0f;

    if (!anyFmOrSync) {
        // === FAST PATH: no FM, no sync — simple independent oscillator rendering ===
        int16_t fastBuf[MAX_BLOCK_SIZE];
        for (int oscIndex = 0; oscIndex < NUM_OSC; ++oscIndex) {
            osc[oscIndex].setDecimation(static_cast<uint32_t>(sampleReduceFactor));
            float modulatedMorph = oscMorph[oscIndex];
            if (oscIndex == 0) modulatedMorph += modOffsets[kDestOsc1Morph];
            else if (oscIndex == 1) modulatedMorph += modOffsets[kDestOsc2Morph];
            else if (oscIndex == 2) modulatedMorph += modOffsets[kDestOsc3Morph];
            modulatedMorph += modOffsets[kDestAllMorph];
            modulatedMorph = std::clamp(modulatedMorph, 0.0f, 1.0f);
            // For square waveforms, morph controls pulse width
            if (oscWaveform[oscIndex] == 1 || oscWaveform[oscIndex] == 6) {
                osc[oscIndex].setPulseWidth(0.05f + modulatedMorph * 0.9f);
            } else {
                osc[oscIndex].setShapeMorph(modulatedMorph);
            }
            osc[oscIndex].prepareFmBlock(nullptr, numSamples);
            switch (oscWaveform[oscIndex]) {
                case 0: osc[oscIndex].getSineWaveBlock(fastBuf, numSamples); break;
                case 1: osc[oscIndex].getSquareWaveBlock(fastBuf, numSamples); break;
                case 2: osc[oscIndex].getTriangleWaveBlock(fastBuf, numSamples); break;
                case 3: osc[oscIndex].getSawWaveBlock(fastBuf, numSamples); break;
                case 4: osc[oscIndex].getMorphedWaveBlock(fastBuf, numSamples); break;
                case 5: osc[oscIndex].getPolyBlepSawWaveBlock(fastBuf, numSamples); break;
                case 6: osc[oscIndex].getPolyBlepSquareWaveBlock(fastBuf, numSamples); break;
                case 7: osc[oscIndex].getWavetableWaveBlock(fastBuf, numSamples); break;
                case 8: osc[oscIndex].getNoiseWaveBlock(fastBuf, numSamples); break;
                default: osc[oscIndex].getSawWaveBlock(fastBuf, numSamples); break;
            }
            float level = std::clamp(oscLevel[oscIndex] + modOffsets[kDestOsc1Level + oscIndex], 0.0f, 1.0f);
            for (int i = 0; i < numSamples; ++i) {
                voiceBuffer[i] += static_cast<float>(fastBuf[i]) * q15ToFloat * level;
            }
        }
    } else {
        // === FM/SYNC PATH: directed rendering with dependency order ===
        int16_t osc2Buffer[MAX_BLOCK_SIZE];
        int16_t osc1Buffer[MAX_BLOCK_SIZE];
        int16_t osc0Buffer[MAX_BLOCK_SIZE];
        bool sync2[MAX_BLOCK_SIZE];
        bool sync1[MAX_BLOCK_SIZE];

        // Helper lambda for FM/sync-aware oscillator rendering
        auto renderOsc = [&](int idx, int16_t* outBuf, bool* syncOut, const bool* syncIn, const int16_t* fmIn, uint32_t n) {
            osc[idx].setDecimation(static_cast<uint32_t>(sampleReduceFactor));
            float modulatedMorph = oscMorph[idx];
            if (idx == 0) modulatedMorph += modOffsets[kDestOsc1Morph];
            else if (idx == 1) modulatedMorph += modOffsets[kDestOsc2Morph];
            else if (idx == 2) modulatedMorph += modOffsets[kDestOsc3Morph];
            modulatedMorph += modOffsets[kDestAllMorph];
            modulatedMorph = std::clamp(modulatedMorph, 0.0f, 1.0f);
            // For square waveforms, morph controls pulse width
            if (oscWaveform[idx] == 1 || oscWaveform[idx] == 6) {
                osc[idx].setPulseWidth(0.05f + modulatedMorph * 0.9f);
            } else {
                osc[idx].setShapeMorph(modulatedMorph);
            }
            osc[idx].prepareFmBlock(fmIn, n);
            osc[idx].getWaveBlockWithSync(outBuf, syncOut, syncIn,
                static_cast<OscillatorFixedPoint::WaveformType>(oscWaveform[idx]), n);
        };

        // Determine which sync outputs are actually needed
        bool needSync2 = syncEnable3to2 || syncEnable3to1;
        bool needSync1 = syncEnable2to1;

        // Render osc[2] first (pure, no FM input)
        osc[2].setFmDepth(0.0f);
        renderOsc(2, osc2Buffer, needSync2 ? sync2 : nullptr, nullptr, nullptr, numSamples);

        // Render osc[1] with FM from osc[2]
        {
            float fm3to2 = fmDepth3to2 + modOffsets[kDestFM3to2] * 10000.0f;
            fm3to2 = std::max(fm3to2, 0.0f);
            osc[1].setFmDepth(fm3to2);
            const bool* syncIn1 = syncEnable3to2 ? sync2 : nullptr;
            renderOsc(1, osc1Buffer, needSync1 ? sync1 : nullptr, syncIn1, (fm3to2 > 0.0f) ? osc2Buffer : nullptr, numSamples);
        }

        // Render osc[0] with FM from osc[2] and osc[1]
        {
            float fm3to1 = fmDepth3to1 + modOffsets[kDestFM3to1] * 10000.0f;
            float fm2to1 = fmDepth2to1 + modOffsets[kDestFM2to1] * 10000.0f;
            fm3to1 = std::max(fm3to1, 0.0f);
            fm2to1 = std::max(fm2to1, 0.0f);
            float totalDepth = fm3to1 + fm2to1;

            // Combine sync triggers only if needed
            bool combinedSync[MAX_BLOCK_SIZE];
            bool hasSyncIn = syncEnable3to1 || syncEnable2to1;
            if (hasSyncIn) {
                for (int i = 0; i < numSamples; ++i) {
                    combinedSync[i] = (syncEnable3to1 && sync2[i]) || (syncEnable2to1 && sync1[i]);
                }
            }

            if (totalDepth > 0.0f) {
                int16_t combinedFm[MAX_BLOCK_SIZE];
                float w3 = fm3to1 / totalDepth;
                float w1 = fm2to1 / totalDepth;
                for (int i = 0; i < numSamples; ++i) {
                    float mixed = static_cast<float>(osc2Buffer[i]) * w3 + static_cast<float>(osc1Buffer[i]) * w1;
                    combinedFm[i] = static_cast<int16_t>(std::clamp(mixed, -32768.0f, 32767.0f));
                }
                osc[0].setFmDepth(totalDepth);
                renderOsc(0, osc0Buffer, nullptr, hasSyncIn ? combinedSync : nullptr, combinedFm, numSamples);
            } else {
                osc[0].setFmDepth(0.0f);
                renderOsc(0, osc0Buffer, nullptr, hasSyncIn ? combinedSync : nullptr, nullptr, numSamples);
            }
        }

        // Sum oscillators to voice buffer
        for (int idx = 0; idx < NUM_OSC; ++idx) {
            const int16_t* buf = (idx == 0) ? osc0Buffer : (idx == 1) ? osc1Buffer : osc2Buffer;
            float level = std::clamp(oscLevel[idx] + modOffsets[kDestOsc1Level + idx], 0.0f, 1.0f);
            for (int i = 0; i < numSamples; ++i) {
                voiceBuffer[i] += static_cast<float>(buf[i]) * q15ToFloat * level;
            }
        }
    }

    float modulatedFilterEnvAmount = filterEnvAmount + modOffsets[kDestFilterEnvAmount] * 10000.0f;
    float envMod = filterEnv.getTargetLevelShaped() * modulatedFilterEnvAmount;

    // Velocity sensitivity: 1.0 = full dynamic range, 0.0 = all notes at max
    float effectiveVel = 1.0f - velocitySens * (1.0f - velocity);

    // Pitch-space cutoff modulation:
    // baseCutoff is in Hz. Convert env + mod to octave offsets, then multiply.
    // Env amount: treat as additive Hz bias on baseCutoff, then convert to octaves.
    float cutoffHz = baseCutoff + (envMod * effectiveVel);

    // Keyboard tracking: shift cutoff in octaves relative to middle C (note 60)
    if (keyboardTracking > 0.0f && note >= 0) {
        float noteOctaves = (note - 60) / 12.0f; // semitones → octaves
        cutoffHz *= fast_exp2f(keyboardTracking * noteOctaves);
    }

    // Mod matrix cutoff: ±4 octaves at full depth (pitch-space, not Hz-space)
    // plus direct LFO1→Cutoff: ±4 octaves at full depth
    float cutoffModOctaves = modOffsets[kDestCutoff] * 4.0f;
    if (std::fabs(lfo1CutoffMod) > 0.001f) {
        cutoffModOctaves += voiceLfoValue * lfo1CutoffMod * 4.0f;
    }
    cutoffHz *= fast_exp2f(cutoffModOctaves);

    float modulatedCutoff = std::clamp(cutoffHz, 20.0f, 20000.0f);
    float modulatedResonance = resonance + modOffsets[kDestResonance];
    modulatedResonance = std::clamp(modulatedResonance, 0.0f, 1.0f);
    FilterMode mode = static_cast<FilterMode>(filterMode);
    // Auto-bypass: skip filter when wide open and no resonance
    if (mode != FilterMode::BYPASS) {
        if (modulatedCutoff < 19500.0f || modulatedResonance >= 0.01f) {
            float modulatedDrive = std::clamp(drive + modOffsets[kDestDrive] * 9.0f, 1.0f, 10.0f);
            filter.processBlock(voiceBuffer, voiceBuffer, numSamples, modulatedCutoff, modulatedResonance, modulatedDrive, mode);

            // Resonance gain compensation (SVF only — ladder/MS-20/diode have internal comp)
            if (filterModel == 0 && modulatedResonance > 0.01f) {
                float resoCompGain = 1.0f / (1.0f + modulatedResonance * 2.0f);
                for (int i = 0; i < numSamples; ++i)
                    voiceBuffer[i] *= resoCompGain;
            }
        }
    }

    // Bit crusher: reduce bit depth (sample-rate decimation is now in the oscillator)
    if (bitCrushBits < 16) {
        float crushLevels = static_cast<float>(1 << bitCrushBits);
        for (int i = 0; i < numSamples; ++i) {
            voiceBuffer[i] = std::round(voiceBuffer[i] * crushLevels) / crushLevels;
        }
    }

    float startAmp = ampEnv.getCurrentLevelShaped() * effectiveVel;
    float targetAmp = ampEnv.getTargetLevelShaped() * effectiveVel;
    float ampStep = (targetAmp - startAmp) / static_cast<float>(numSamples);
    float currentAmp = startAmp;

    // Apply amp envelope + steal fade to voice buffer BEFORE delay
    // This means the delay receives the enveloped signal; echoes
    // recirculate and decay naturally even after the envelope ends.
    for (int i = 0; i < numSamples; ++i) {
        currentAmp += ampStep;

        float stealFade = 1.0f;
        if (stealFadeCounter > 0) {
            stealFade = 1.0f - (static_cast<float>(stealFadeCounter) / static_cast<float>(STEAL_FADE_SAMPLES));
            stealFadeCounter--;
        }

        voiceBuffer[i] *= currentAmp * stealFade;
    }

    // Modulate delay parameters from mod matrix
    // Fixed absolute ±10ms modulation (§11e) — no more proportional scaling
    float delayModSamples = modOffsets[kDestDelayTime] * (kDelayModMaxMs * 0.001f * voiceSampleRate);
    float targetDelaySamples = std::clamp(delaySamples + delayModSamples, 1.0f, static_cast<float>(DELAY_SIZE - 1));
    float modulatedDelayFeedback = std::clamp(delayFeedback + modOffsets[kDestDelayFeedback], 0.0f, 0.99f);
    float modulatedDelayMix = std::clamp(delayMix + modOffsets[kDestDelayMix], 0.0f, 1.0f);

    bool delayActive = (modulatedDelayMix >= 0.001f);

    float energy = 0.0f;
    if (delayActive) {
        // Per-sample delay ramp for click-free modulation (§11e)
        float smoothStep = (targetDelaySamples - voiceDelay._smoothedDelay) / static_cast<float>(numSamples);
        for (int i = 0; i < numSamples; ++i) {
            voiceDelay._smoothedDelay += smoothStep;
            float raw;
            float wet = voiceDelay.process(voiceBuffer[i], voiceDelay._smoothedDelay, modulatedDelayFeedback, modulatedDelayMix, &raw);
            out[i] += wet;
            energy += raw * raw;  // measure RAW delay energy, not mix-scaled
        }
    } else {
        for (int i = 0; i < numSamples; ++i) {
            out[i] += voiceBuffer[i];
        }
    }
    delayEnergy = energy / static_cast<float>(numSamples);

    ampEnv.finalizeBlock();
    filterEnv.finalizeBlock();
    modEnv.finalizeBlock();
}

// ============================================================================
// Private helpers
// ============================================================================

void PolyLofiVoice::renderStealTail() {
    // Clear previous tail state so processBlock doesn't re-mix old tail
    stealTailRemaining = 0;
    stealTailReadPos = 0;

    for (int i = 0; i < STEAL_FADE_SAMPLES; ++i) stealTailBuf[i] = 0.0f;

    // Empty mod matrix — all slots disabled (sourceIdx defaults to -1)
    ModSlot emptyMatrix[NUM_MOD_SLOTS];

    // Render old voice in MAX_BLOCK_SIZE chunks
    int rendered = 0;
    while (rendered < STEAL_FADE_SAMPLES) {
        int chunk = std::min(static_cast<int>(MAX_BLOCK_SIZE),
                             STEAL_FADE_SAMPLES - rendered);
        processBlock(stealTailBuf + rendered, chunk, emptyMatrix);
        rendered += chunk;
    }

    // Apply linear fade-out (1 → 0)
    float invLen = 1.0f / static_cast<float>(STEAL_FADE_SAMPLES);
    for (int i = 0; i < STEAL_FADE_SAMPLES; ++i) {
        stealTailBuf[i] *= 1.0f - static_cast<float>(i) * invLen;
    }

    stealTailRemaining = STEAL_FADE_SAMPLES;
    stealTailReadPos = 0;
}

void PolyLofiVoice::updateOscFrequencies() {
    for (int i = 0; i < NUM_OSC; ++i) {
        float semitoneFreq = ((float)note - 69.0f + oscSemitone[i] + pitchBendSemitones) / 12.0f;
        float freq = 440.0f * fast_powf(2.0f, semitoneFreq) * fast_powf(2.0f, oscFine[i] / 1200.0f);
        currentFreq[i] = freq;
        targetFreq[i] = freq;
        osc[i].setFrequency(freq);
        osc[i].hardSync();
    }
    glideBlocksRemaining = 0;
}
