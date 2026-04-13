#pragma once

#include "LFO.h"            // LFO (transitively includes LofiMorphOscillator.h / OscillatorFixedPoint)
#include "ShapedADSR.h"     // Assuming your ShapedADSR class
#include "ZDFFilter.h"              // Unified ZDF filter (SVF + Ladder models)
#include "DecimatedDelay.h" // New decimated delay for lo-fi echo effects
#include "PolyLofiParams.h" // Param enum, pitch track multipliers, string tables
#include <cmath>
#include <algorithm>

#define NUM_MOD_SLOTS 4

struct ModSlot {
    int8_t sourceIdx = -1; // Index of the source (LFO, Env, Velocity, etc.)
    int8_t destIdx = -1;   // Index of the destination (Cutoff, Pitch, Morph)
    float amount = 0.0f;   // Bipolar depth
};

enum ModDest {
    kDestCutoff,
    kDestResonance,
    kDestAmpAttack,
    kDestAmpDecay,
    kDestAmpRelease,
    kDestFilterAttack,
    kDestFilterDecay,
    kDestFilterRelease,
    kDestOsc1Morph,
    kDestOsc2Morph,
    kDestOsc3Morph,
    kDestAllMorph,
    kDestFM3to2,
    kDestFM3to1,
    kDestFM2to1,
    kDestDelayTime,
    kDestDelayFeedback,
    kDestDelayMix,
    kDestPitch,
    kDestDrive,
    kDestFilterEnvAmount,
    kDestOsc1Level,
    kDestOsc2Level,
    kDestOsc3Level,
    kDestOsc1Pitch,
    kDestOsc2Pitch,
    kDestOsc3Pitch,
    kDestLfoSpeed,
    kNumDests
};

enum ModSource {
    kSourceOff,
    kSourceLFO,
    kSourceLFO2,
    kSourceLFO3,
    kSourceAmpEnv,
    kSourceFilterEnv,
    kSourceModEnv,
    kSourceVelocity,
    kSourceModWheel,
    kSourceAftertouch,
    kSourceNoteRandom, // New: random value per note-on
    kSourceKeyTracking, // Bipolar key tracking: (note - 60) / 60
    kNumSources
};

class PolyLofiVoice {
public:
    static const int NUM_OSC = 3;
    static const int STEAL_FADE_SAMPLES = 256; // ~5.8ms crossfade when voice is stolen

    explicit PolyLofiVoice(float* delayBuffer);

    // Setup
    void setSampleRate(float sr, float blockSize);
    void setFilterModel(FilterModel m) { filterModel = static_cast<int>(m); filter.setModel(m); }

    // Envelope parameters
    void setAmpEnv(float a, float d, float s, float r);
    void setAmpShape(float shape);
    void setFilterEnv(float a, float d, float s, float r);
    void setFilterShape(float shape);
    void setModEnv(float a, float d, float s, float r);
    void setModShape(float shape);

    // Delay diffuser
    void initDelayDiffuser(float* diffuserBuf);
    void setDelayDiffusion(float d);

    // LFO parameters
    void setLfoFrequency(float freq);
    void setLfoFrequency(int lfoIdx, float freq);
    void setLfoShape(int lfoIdx, int shape);
    void setLfoUnipolar(int lfoIdx, bool unipolar);
    void setLfoMorph(int lfoIdx, float morph);
    void setLfoSampleHoldRate(int lfoIdx, float hz);

    // Oscillator parameters
    void setOscillatorParameters(int index, int waveform, float semitoneOffset, float fineOffset, float morph, float level);
    void setOscWavetable(int oscIdx, const int16_t* data, uint32_t numWaves, uint32_t waveLength);

    // Note lifecycle
    void noteOn(int midiNote, float vel);
    void legatoRetrigger(int midiNote, float vel);
    void stealVoice(int midiNote, float vel);
    void noteOff();
    void releaseSustain();
    void setSustainPedal(bool down);
    bool isAmpGated() const;

    // Performance controls
    void setPitchBend(float semitones);
    void setModWheel(float value);
    void setAftertouch(float value);
    void setPan(float p);

    // Voice state queries
    float getCurrentAmplitudeLevel() const;

    // Audio processing
    void processBlock(float* out, int numSamples, const ModSlot* matrix);

    // --- Public member data ---
    bool active;
    int note;
    float velocity;
    int stealFadeCounter = 0; // Counts down during fade-in when voice is stolen

    // Pre-rendered crossfade tail from the previous (stolen) voice.
    // Filled by renderStealTail(), mixed into output in processBlock().
    float stealTailBuf[STEAL_FADE_SAMPLES] = {};
    int   stealTailRemaining = 0;
    int   stealTailReadPos = 0;

    float baseCutoff = 1000.0f;
    float resonance = 0.1f;
    float filterEnvAmount = 5000.0f;
    int filterMode = 0; // LP2 by default
    int filterModel = 0; // 0=SVF, 1=Ladder, 2=MS20, 3=Diode
    float drive = 1.0f;
    float keyboardTracking = 0.0f; // 0.0–1.0 (0–100%)
    float delaySamples = 0.0f;
    float delayFeedback = 0.0f;
    float delayMix = 0.0f;
    float delayDiffusion = 0.0f;
    float delayEnergy = 0.0f;  // Tracks delay tail energy for voice lifetime / stealing
    int   delayPitchTrackMode = 0; // 0=Off, 1=Unison, 2=Oct-1, 3=Oct+1, 4=Fifth
    static constexpr float kDelayModMaxMs = 10.0f; // Fixed +/-10ms delay modulation range
    float baseAmpShape = 0.0f;
    float baseFilterShape = 0.0f;
    float baseAmpAttack = 0.01f;
    float baseAmpDecay = 0.1f;
    float baseAmpRelease = 0.2f;
    float baseAmpSustain = 0.8f;
    float baseFilterAttack = 0.01f;
    float baseFilterDecay = 0.1f;
    float baseFilterRelease = 0.2f;
    float baseFilterSustain = 0.8f;
    float baseModAttack = 0.01f;
    float baseModDecay = 0.1f;
    float baseModRelease = 0.2f;
    float baseModSustain = 0.8f;
    float baseModShape = 0.0f;

    // Pitch bend (in semitones, typically +/-2)
    float pitchBendSemitones = 0.0f;

    // Mod wheel (0.0 to 1.0)
    float modWheelValue = 0.0f;

    // Aftertouch (0.0 to 1.0)
    float aftertouchValue = 0.0f;

    // Sustain pedal state
    bool sustainPedalDown = false;
    bool sustainHeldOff = false;

    // LFO key sync (reset phase on noteOn)
    bool lfoKeySync[3] = {false, false, false};

    // Base LFO frequencies (stored for mod matrix speed modulation)
    float baseLfoFreq[3] = {5.0f, 5.0f, 5.0f};
    bool lfoSpeedModActive = false;

    // FM routing depths (Hz deviation)
    float fmDepth3to2 = 0.0f;
    float fmDepth3to1 = 0.0f;
    float fmDepth2to1 = 0.0f;

    // Hard sync enables
    bool syncEnable3to2 = false;
    bool syncEnable3to1 = false;
    bool syncEnable2to1 = false;

    // Glide / Portamento
    float glideTimeMs = 0.0f;       // Glide time in ms (0 = off)
    int glideMode = 0;              // 0=Off, 1=Always, 2=Legato Only

    // Direct mod amounts (bypass mod matrix)
    float lfo1CutoffMod = 0.0f;    // LFO1→Cutoff depth (-1..+1 → ±4 octaves)
    float lfo2VibratoMod = 0.0f;   // LFO2→Pitch depth in cents
    float velocitySens = 1.0f;     // 0=fixed full volume, 1=full velocity control

    // Stereo pan (constant-power)
    float panL = 0.70710678f;       // cos(pi/4) = center
    float panR = 0.70710678f;       // sin(pi/4) = center

    // Bit Crusher
    int bitCrushBits = 16;          // Bit depth (1-16, 16 = no crush)
    int sampleReduceFactor = 1;     // Sample rate reduction (1 = no reduction)

    int oscWaveform[NUM_OSC];
    float oscSemitone[NUM_OSC];
    float oscFine[NUM_OSC];
    float oscLevel[NUM_OSC];
    float oscMorph[NUM_OSC];

    DecimatedDelay voiceDelay;  // Public for plugin-level feedback filter config

private:
    float noteRandom = 0.0f;
    void renderStealTail();
    void updateOscFrequencies();

    float currentFreq[NUM_OSC] = {440.0f, 440.0f, 440.0f};
    float targetFreq[NUM_OSC] = {440.0f, 440.0f, 440.0f};
    float glideFreqStep[NUM_OSC] = {0.0f, 0.0f, 0.0f};
    int glideBlocksRemaining = 0;

    float voiceSampleRate = 44100.0f;

    OscillatorFixedPoint osc[NUM_OSC];
    LFO lfo[NUM_OSC];
    ShapedADSR ampEnv;
    ShapedADSR filterEnv;
    ShapedADSR modEnv;
    ZDFFilter filter;
    
    float modOffsets[kNumDests] = {};
    float modSources[kNumSources] = {};
};
