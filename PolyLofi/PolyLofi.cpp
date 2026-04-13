#include <math.h>
#include <cmath>
#include <new>
#include <cstring>
#include <distingnt/api.h>
#include <distingnt/wav.h>
#include <distingnt/serialisation.h>
#include <array>
#include "PolyLofiVoice.h"
#include "PolyLofiParams.h"
#include "MidiClockTracker.h"
#include "VoiceAllocator.h"
#include "CheapMaths.h"
#include "WavetableManager.h"
#include "PolyLofiPresets.h"

// Debug overlay: set to 1 for hardware bringup, 0 for release.
// Enables on-screen diagnostics (voice count, peak amp, NaN counter, etc.)
// in draw() and lightweight guards in step(). Formatting only in draw(),
// so audio-thread cost is a handful of float stores + one isfinite branch
// per output sample.
#ifndef POLYLOFI_DEBUG
#define POLYLOFI_DEBUG 0
#endif

#define MAX_VOICES 12
#define MAX_BLOCK_SIZE 64

#define NUM_MOD_SLOTS 4

struct _polyLofiAlgorithm_DTC
{
    PolyLofiVoice* voices[MAX_VOICES];
    float baseCutoff = 1000.0f;
    float resonance = 0.1f;
    float filterEnvAmount = 5000.0f;
    float drive = 1.0f;
    float delayTimeMs = 500.0f;
    float delaySamples = NT_globals.sampleRate * (delayTimeMs * 0.001f); // default 500ms at full sample rate
    float delayFeedback = 0.25f;
    float delayMix = 0.25f;
    float delayDiffusion = 0.0f;
    int filterMode = 0; // LP2 by default
    int filterModel = 0; // 0=SVF, 1=Ladder
    float keyboardTracking = 0.0f; // 0.0–1.0
    int delayFBFilterMode = 0;     // 0=Off, 1=LP, 2=HP
    float delayFBFreq = 3000.0f;   // Feedback filter cutoff Hz
    int delayPitchTrackMode = 0;   // 0=Off, 1=Unison, 2=Oct-1, 3=Oct+1, 4=Fifth
    float ampA = 0.01f, ampD = 0.1f, ampS = 0.8f, ampR = 0.5f;
    float ampShape = 0.0f;
    float filterA = 0.05f, filterD = 0.1f, filterS = 0.8f, filterR = 0.2f;
    float filterShape = 0.0f;
    int midiChannel = 0;
    int lastVoiceIndex = 0; // For round-robin voice stealing

    int oscWaveform[PolyLofiVoice::NUM_OSC] = {3, 3, 3};
    float oscSemitone[PolyLofiVoice::NUM_OSC] = {0.0f, 0.0f, 0.0f};
    float oscFine[PolyLofiVoice::NUM_OSC] = {0.0f, 0.0f, 0.0f};
    float oscLevel[PolyLofiVoice::NUM_OSC] = {0.3333f, 0.3333f, 0.3333f};
    float oscMorph[PolyLofiVoice::NUM_OSC] = {0.0f, 0.0f, 0.0f};
    float oscPulseWidth_UNUSED = 0; // removed — morph controls PW on square waveforms
    ModSlot matrix[NUM_MOD_SLOTS];
    float lfoSpeed[3] = {5.0f, 5.0f, 5.0f}; // 5 Hz default for all 3
    int lfoShape[3] = {0, 0, 0}; // SHAPE_SINE by default for all 3
    bool lfoUnipolar[3] = {false, false, false};
    float lfoMorph[3] = {0.0f, 0.0f, 0.0f};
    float modA = 0.01f, modD = 0.1f, modS = 0.8f, modR = 0.2f;
    float modShape = 0.0f;
    float fmDepth3to2 = 0.0f;
    float fmDepth3to1 = 0.0f;
    float fmDepth2to1 = 0.0f;
    bool syncEnable3to2 = false;
    bool syncEnable3to1 = false;
    bool syncEnable2to1 = false;

    // MIDI controller state
    float pitchBendSemitones = 0.0f;     // Current pitch bend in semitones
    float pitchBendRange = 2.0f;          // Bend range in semitones (±)
    float modWheelValue = 0.0f;           // CC1 mod wheel (0.0-1.0)
    float aftertouchValue = 0.0f;         // Channel aftertouch (0.0-1.0)
    bool sustainPedalDown = false;        // CC64 sustain pedal

    // Glide / Portamento
    float glideTimeMs = 0.0f;
    int glideMode = 0; // 0=Off, 1=Always, 2=Legato

    // Legato mode (mono with no envelope retrigger)
    bool legato = false;

    // Direct mod amounts (bypass mod matrix)
    float lfo1CutoffMod = 0.0f;   // LFO1→Cutoff depth (-1..+1 → ±4 octaves)
    float lfo2VibratoMod = 0.0f;  // LFO2→Pitch depth in cents
    float velocitySens = 1.0f;    // 0=fixed full volume, 1=full velocity control

    // Voice count (set from specification at construct time)
    int numVoices = 8;

    // Bit Crusher
    int bitCrushBits = 16;
    int sampleReduceFactor = 1;

    // MIDI clock sync
    MidiClockTracker clockTracker;
    int      lfoSyncMode[3] = {0, 0, 0}; // 0=Free, 1-11 = synced divisions
    bool     lfoKeySync[3]  = {false, false, false}; // per-LFO note retrigger
    int      delaySyncMode = 0;           // 0=Free, 1-11 = synced to clock

    // CV clock tracking
    float    prevClockValue = 0.0f;
    uint32_t samplesSinceLastCvClock = 0;
    float    cvClockBpm = 0.0f;
    bool     cvClockActive = false;

    // Wavetable manager
    WavetableManager wtManager;

    // Master output gain (0..5V, default 3.5V = 70%)
    float masterGain = 3.5f;

    // Stereo pan spread
    float panSpread = 0.0f;         // 0.0 (mono center) to 1.0 (full L-R spread)

    // Preset bank (10 slots)
    PresetBank presets;

    // Delayed auto-reset for Save confirm (holds "On" visibly)
    int saveResetCountdown = 0;

#if POLYLOFI_DEBUG
    // --- Hardware debug overlay (R6f) ---
    // Written in step()/midiMessage(), read in draw() at screen rate.
    float    dbgActiveVoices  = 0.f;  // voices with active==true this block
    float    dbgPeakAmp       = 0.f;  // max |sample| in last output block
    float    dbgLastNote      = 0.f;  // MIDI note of most recent noteOn
    uint32_t dbgFrameCounter  = 0;    // increments each step() call
    uint32_t dbgNanCount      = 0;    // cumulative NaN/Inf samples clamped
    uint32_t dbgBusErrors     = 0;    // cumulative bad bus-index hits
#endif
};

struct _polyLofiAlgorithm : public _NT_algorithm
{
	_polyLofiAlgorithm( _polyLofiAlgorithm_DTC* dtc_ ) : dtc( dtc_ ) {}
	~_polyLofiAlgorithm() {}
	
	_polyLofiAlgorithm_DTC*	dtc;
};

static const _NT_parameter	parameters[] = {
     NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Output", 1, 13 )
    { "Cutoff", 0, 10000, 5000, kNT_unitNone, 0, NULL },
    { "Resonance", 0, 1000, 100, kNT_unitNone, kNT_scaling1000, NULL },
    { "Filter Env Amount", 0, 10000, 5000, kNT_unitNone, 0, NULL },
    { "Filter Mode", 0, 6, 0, kNT_unitEnum, 0, enumStringsFilterMode },
    { "Drive", 1000, 10000, 1000, kNT_unitNone, kNT_scaling1000, NULL },
    { "Osc1 Waveform", 0, 8, 3, kNT_unitEnum, 0, enumStringsWaveform },
    { "Osc1 Semitone", -48, 48, 0, kNT_unitSemitones, 0, NULL },
    { "Osc1 Fine", -100, 100, 0, kNT_unitCents, 0, NULL },
    { "Osc1 Morph", 0, 1000, 0, kNT_unitNone, 0, NULL },
    { "Osc1 Level", 0, 1000, 333, kNT_unitNone, kNT_scaling1000, NULL },
    { "Osc2 Waveform", 0, 8, 3, kNT_unitEnum, 0, enumStringsWaveform },
    { "Osc2 Semitone", -48, 48, 0, kNT_unitSemitones, 0, NULL },
    { "Osc2 Fine", -100, 100, 0, kNT_unitCents, 0, NULL },
    { "Osc2 Morph", 0, 1000, 0, kNT_unitNone, 0, NULL },
    { "Osc2 Level", 0, 1000, 333, kNT_unitNone, kNT_scaling1000, NULL },
    { "Osc3 Waveform", 0, 8, 3, kNT_unitEnum, 0, enumStringsWaveform },
    { "Osc3 Semitone", -48, 48, 0, kNT_unitSemitones, 0, NULL },
    { "Osc3 Fine", -100, 100, 0, kNT_unitCents, 0, NULL },
    { "Osc3 Morph", 0, 1000, 0, kNT_unitNone, 0, NULL },
    { "Osc3 Level", 0, 1000, 333, kNT_unitNone, kNT_scaling1000, NULL },
    { "Amp Attack", 0, 3000, 10, kNT_unitMs, kNT_scaling1000, NULL },
    { "Amp Decay", 0, 3000, 100, kNT_unitMs, kNT_scaling1000, NULL },
    { "Amp Sustain", 0, 1000, 800, kNT_unitNone, kNT_scaling1000, NULL },
    { "Amp Release", 0, 3000, 500, kNT_unitMs, kNT_scaling1000, NULL },
    { "Amp Shape", -990, 990, 0, kNT_unitNone, kNT_scaling1000, NULL },
    { "Delay Time", 0, 1000, 500, kNT_unitMs, kNT_scaling1000, NULL },
    { "Delay Feedback", 0, 1000, 250, kNT_unitNone, kNT_scaling1000, NULL },
    { "Delay Mix", 0, 1000, 250, kNT_unitNone, kNT_scaling1000, NULL },
    { "Filter Attack", 0, 3000, 50, kNT_unitMs, kNT_scaling1000, NULL },
    { "Filter Decay", 0, 3000, 100, kNT_unitMs, kNT_scaling1000, NULL },
    { "Filter Sustain", 0, 1000, 800, kNT_unitNone, kNT_scaling1000, NULL },
    { "Filter Release", 0, 3000, 200, kNT_unitMs, kNT_scaling1000, NULL },
    { "Filter Shape", -990, 990, 0, kNT_unitNone, kNT_scaling1000, NULL },
    { "MIDI Channel", 0, 16, 0, kNT_unitEnum, 0, enumStringsMidiChannel },
    // LFO1
    { "LFO1 Speed", 0, 1000, 500, kNT_unitNone, kNT_scaling1000, NULL },
    { "LFO1 Shape", 0, 5, 0, kNT_unitEnum, 0, enumStringsLfoShape },
    { "LFO1 Unipolar", 0, 1, 0, kNT_unitEnum, 0, enumStringsOnOff },
    { "LFO1 Morph", 0, 1000, 0, kNT_unitNone, kNT_scaling1000, NULL },
    { "LFO1 Sync Mode", 0, 11, 0, kNT_unitEnum, 0, enumStringsLfoSyncMode },
    { "LFO1 Key Sync", 0, 1, 0, kNT_unitEnum, 0, enumStringsOnOff },
    // LFO2
    { "LFO2 Speed", 0, 1000, 500, kNT_unitNone, kNT_scaling1000, NULL },
    { "LFO2 Shape", 0, 5, 0, kNT_unitEnum, 0, enumStringsLfoShape },
    { "LFO2 Unipolar", 0, 1, 0, kNT_unitEnum, 0, enumStringsOnOff },
    { "LFO2 Morph", 0, 1000, 0, kNT_unitNone, kNT_scaling1000, NULL },
    { "LFO2 Sync Mode", 0, 11, 0, kNT_unitEnum, 0, enumStringsLfoSyncMode },
    { "LFO2 Key Sync", 0, 1, 0, kNT_unitEnum, 0, enumStringsOnOff },
    // LFO3
    { "LFO3 Speed", 0, 1000, 500, kNT_unitNone, kNT_scaling1000, NULL },
    { "LFO3 Shape", 0, 5, 0, kNT_unitEnum, 0, enumStringsLfoShape },
    { "LFO3 Unipolar", 0, 1, 0, kNT_unitEnum, 0, enumStringsOnOff },
    { "LFO3 Morph", 0, 1000, 0, kNT_unitNone, kNT_scaling1000, NULL },
    { "LFO3 Sync Mode", 0, 11, 0, kNT_unitEnum, 0, enumStringsLfoSyncMode },
    { "LFO3 Key Sync", 0, 1, 0, kNT_unitEnum, 0, enumStringsOnOff },
    { "Mod1 Source", 0, 11, 0, kNT_unitEnum, 0, enumStringsModSource },
    { "Mod1 Dest", 0, 27, 0, kNT_unitEnum, 0, enumStringsModDest },
    { "Mod1 Amount", -1000, 1000, 0, kNT_unitNone, kNT_scaling1000, NULL },
    { "Mod2 Source", 0, 11, 0, kNT_unitEnum, 0, enumStringsModSource },
    { "Mod2 Dest", 0, 27, 0, kNT_unitEnum, 0, enumStringsModDest },
    { "Mod2 Amount", -1000, 1000, 0, kNT_unitNone, kNT_scaling1000, NULL },
    { "Mod3 Source", 0, 11, 0, kNT_unitEnum, 0, enumStringsModSource },
    { "Mod3 Dest", 0, 27, 0, kNT_unitEnum, 0, enumStringsModDest },
    { "Mod3 Amount", -1000, 1000, 0, kNT_unitNone, kNT_scaling1000, NULL },
    { "Mod4 Source", 0, 11, 0, kNT_unitEnum, 0, enumStringsModSource },
    { "Mod4 Dest", 0, 27, 0, kNT_unitEnum, 0, enumStringsModDest },
    { "Mod4 Amount", -1000, 1000, 0, kNT_unitNone, kNT_scaling1000, NULL },
    { "Mod Env Attack", 0, 3000, 10, kNT_unitMs, kNT_scaling1000, NULL },
    { "Mod Env Decay", 0, 3000, 100, kNT_unitMs, kNT_scaling1000, NULL },
    { "Mod Env Sustain", 0, 1000, 800, kNT_unitNone, kNT_scaling1000, NULL },
    { "Mod Env Release", 0, 3000, 200, kNT_unitMs, kNT_scaling1000, NULL },
    { "Mod Env Shape", -990, 990, 0, kNT_unitNone, kNT_scaling1000, NULL },
    { "FM 3>2 Depth", 0, 10000, 0, kNT_unitNone, 0, NULL },
    { "FM 3>1 Depth", 0, 10000, 0, kNT_unitNone, 0, NULL },
    { "FM 2>1 Depth", 0, 10000, 0, kNT_unitNone, 0, NULL },
    { "Sync 3>2", 0, 1, 0, kNT_unitEnum, 0, enumStringsOnOff },
    { "Sync 3>1", 0, 1, 0, kNT_unitEnum, 0, enumStringsOnOff },
    { "Sync 2>1", 0, 1, 0, kNT_unitEnum, 0, enumStringsOnOff },
    { "Glide Time", 0, 3000, 0, kNT_unitMs, kNT_scaling1000, NULL },
    { "Glide Mode", 0, 2, 0, kNT_unitEnum, 0, enumStringsGlideMode },
    { "Bit Crush", 1, 16, 16, kNT_unitNone, 0, NULL },
    { "Sample Reduce", 1, 32, 1, kNT_unitNone, 0, NULL },
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Right Output", 0, 0 )
    { "Osc1 Wavetable", 0, 255, 0, kNT_unitHasStrings, 0, NULL },
    { "Osc2 Wavetable", 0, 255, 0, kNT_unitHasStrings, 0, NULL },
    { "Osc3 Wavetable", 0, 255, 0, kNT_unitHasStrings, 0, NULL },
    { "Pan Spread", 0, 1000, 0, kNT_unitNone, kNT_scaling1000, NULL },
    { "Delay Sync", 0, 11, 0, kNT_unitEnum, 0, enumStringsLfoSyncMode },
    { "Delay Diffusion", 0, 1000, 0, kNT_unitNone, kNT_scaling1000, NULL },
    NT_PARAMETER_CV_INPUT("Clock Input", 0, 0)
    { "Delay FB Filter", 0, 2, 0, kNT_unitEnum, 0, enumStringsDelayFBFilter },
    { "Delay FB Freq", 200, 18000, 3000, kNT_unitNone, 0, NULL },
    { "Delay Pitch Track", 0, 4, 0, kNT_unitEnum, 0, enumStringsDelayPitchTrack },
    { "Master Volume", 0, 100, 70, kNT_unitPercent, 0, NULL },
    { "Filter Model", 0, 3, 0, kNT_unitEnum, 0, enumStringsFilterModel },
    { "Key Tracking", 0, 1000, 0, kNT_unitPercent, kNT_scaling1000, NULL },
    { "Legato", 0, 1, 0, kNT_unitEnum, 0, enumStringsOnOff },
    { "LFO1>Cutoff", -1000, 1000, 0, kNT_unitNone, kNT_scaling1000, NULL },
    { "LFO2>Vibrato", 0, 100, 0, kNT_unitCents, 0, NULL },
    { "Vel Sens", 0, 100, 100, kNT_unitPercent, 0, NULL },
    { "Load Preset", 0, 13, 0, kNT_unitConfirm, 0, NULL },
    { "Save Slot", 0, 13, 0, kNT_unitHasStrings, 0, NULL },
    { "Save", 0, 1, 0, kNT_unitEnum, 0, enumStringsOnOff },
};

static const uint8_t page1[] = { kParamOsc1Waveform, kParamOsc1Wavetable, kParamOsc1Semitone, kParamOsc1Fine, kParamOsc1Morph, kParamOsc1Level,
                                 kParamOsc2Waveform, kParamOsc2Wavetable, kParamOsc2Semitone, kParamOsc2Fine, kParamOsc2Morph, kParamOsc2Level,
                                 kParamOsc3Waveform, kParamOsc3Wavetable, kParamOsc3Semitone, kParamOsc3Fine, kParamOsc3Morph, kParamOsc3Level,
                                 kParamLfo2VibratoMod };
static const uint8_t page2[] = { kParamBaseCutoff, kParamResonance, kParamFilterEnvAmount, kParamFilterMode, kParamFilterModel, kParamDrive, kParamKeyboardTracking, kParamLfo1CutoffMod, kParamFilterAttack, kParamFilterDecay, kParamFilterSustain, kParamFilterRelease, kParamFilterShape };
static const uint8_t page3[] = { kParamAmpAttack, kParamAmpDecay, kParamAmpSustain, kParamAmpRelease, kParamAmpShape, kParamVelocitySens, kParamGlideTime, kParamGlideMode, kParamLegato };
static const uint8_t page4[] = { kParamOutput, kParamOutputMode, kParamRightOutput, kParamRightOutputMode, kParamMasterVolume, kParamPanSpread, kParamMidiChannel, kParamClockInput, kParamLoadPreset, kParamSavePreset, kParamSaveConfirm };
static const uint8_t page5[] = { kParamLfoSpeed, kParamLfoShape, kParamLfoUnipolar, kParamLfoMorph, kParamLfo1SyncMode, kParamLfo1KeySync, kParamLfo2Speed, kParamLfo2Shape, kParamLfo2Unipolar, kParamLfo2Morph, kParamLfo2SyncMode, kParamLfo2KeySync, kParamLfo3Speed, kParamLfo3Shape, kParamLfo3Unipolar, kParamLfo3Morph, kParamLfo3SyncMode, kParamLfo3KeySync };
static const uint8_t page6[] = { kParamFM3to2, kParamFM3to1, kParamFM2to1, kParamSync3to2, kParamSync3to1, kParamSync2to1, kParamModEnvAttack, kParamModEnvDecay, kParamModEnvSustain, kParamModEnvRelease, kParamModEnvShape };
static const uint8_t page8[] = { kParamMod1Source, kParamMod1Dest, kParamMod1Amount, kParamMod2Source, kParamMod2Dest, kParamMod2Amount, kParamMod3Source, kParamMod3Dest, kParamMod3Amount, kParamMod4Source, kParamMod4Dest, kParamMod4Amount };
static const uint8_t page9[] = { kParamDelayTime, kParamDelaySyncMode, kParamDelayFeedback, kParamDelayMix, kParamDelayDiffusion, kParamDelayFBFilter, kParamDelayFBFreq, kParamDelayPitchTrack, kParamBitCrush, kParamSampleReduce };

static const _NT_parameterPage pages[] = {
    { "Oscs", ARRAY_SIZE(page1), page1 },
    { "Filter", ARRAY_SIZE(page2), page2 },
    { "Amp", ARRAY_SIZE(page3), page3 },
    { "LFOs", ARRAY_SIZE(page5), page5 },
    { "Mod Matrix", ARRAY_SIZE(page8), page8 },
    { "FM/Sync", ARRAY_SIZE(page6), page6 },
    { "Effects", ARRAY_SIZE(page9), page9 },
    { "Setup", ARRAY_SIZE(page4), page4 }
};

static const _NT_parameterPages parameterPages = { ARRAY_SIZE(pages), pages };

static const _NT_specification specs[] = {
    { "Voices", 1, MAX_VOICES, 8, kNT_typeGeneric },
};

void calculateRequirements( _NT_algorithmRequirements& req, const int32_t* specifications )
{
	int numVoices = specifications[0];
	req.numParameters = kNumParams;
	req.sram = sizeof(_polyLofiAlgorithm);
	req.dram = numVoices * DELAY_SIZE * sizeof(float) + numVoices * sizeof(PolyLofiVoice)
	         + WavetableManager::dramBytes()
	         + numVoices * AllpassDiffuser::dramBytes();
	req.dtc = sizeof(_polyLofiAlgorithm_DTC);
	req.itc = 0;
}

_NT_algorithm*	construct( const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req, const int32_t* specifications )
{
    int numVoices = specifications[0];
    _polyLofiAlgorithm_DTC* dtc = new (ptrs.dtc) _polyLofiAlgorithm_DTC();
    dtc->numVoices = numVoices;
    dtc->presets.initDefaults();
    initFactoryPresets(dtc->presets, parameters);
	_polyLofiAlgorithm* alg = new (ptrs.sram) _polyLofiAlgorithm( (_polyLofiAlgorithm_DTC*)ptrs.dtc );
	alg->parameters = parameters;
	alg->parameterPages = &parameterPages;
    {
        dtc->delaySamples = dtc->delayTimeMs * 0.001f * NT_globals.sampleRate;
    }
    // Allocate from DRAM: delay buffers first, then voice objects
    char* dramPtr = (char*)ptrs.dram;
    float* delayBuffers = (float*)dramPtr;
    dramPtr += numVoices * DELAY_SIZE * sizeof(float);
    
    for (int i = 0; i < numVoices; ++i) {
        // Zero the delay buffer
        for (int j = 0; j < DELAY_SIZE; ++j) {
            delayBuffers[i * DELAY_SIZE + j] = 0.0f;
        }
        // Placement new for voice
        PolyLofiVoice* voicePtr = (PolyLofiVoice*)dramPtr;
        dramPtr += sizeof(PolyLofiVoice);
        dtc->voices[i] = voicePtr;
        new (voicePtr) PolyLofiVoice(delayBuffers + i * DELAY_SIZE);
        
        dtc->voices[i]->setSampleRate(NT_globals.sampleRate, NT_globals.maxFramesPerStep);
        dtc->voices[i]->setLfoFrequency(0, dtc->lfoSpeed[0]);
        dtc->voices[i]->setLfoFrequency(1, dtc->lfoSpeed[1]);
        dtc->voices[i]->setLfoFrequency(2, dtc->lfoSpeed[2]);
        dtc->voices[i]->setAmpEnv(dtc->ampA, dtc->ampD, dtc->ampS, dtc->ampR);
        dtc->voices[i]->setAmpShape(dtc->ampShape);
        dtc->voices[i]->setFilterEnv(dtc->filterA, dtc->filterD, dtc->filterS, dtc->filterR);
        dtc->voices[i]->setFilterShape(dtc->filterShape);
        dtc->voices[i]->setModEnv(dtc->modA, dtc->modD, dtc->modS, dtc->modR);
        dtc->voices[i]->setModShape(dtc->modShape);
        dtc->voices[i]->baseCutoff = dtc->baseCutoff;
        dtc->voices[i]->resonance = dtc->resonance;
        dtc->voices[i]->filterEnvAmount = dtc->filterEnvAmount;
        dtc->voices[i]->delaySamples = dtc->delaySamples;
        dtc->voices[i]->delayFeedback = dtc->delayFeedback;
        dtc->voices[i]->delayMix = dtc->delayMix;
        dtc->voices[i]->setDelayDiffusion(dtc->delayDiffusion);
        dtc->voices[i]->voiceDelay.setFeedbackFilter(dtc->delayFBFilterMode, dtc->delayFBFreq, NT_globals.sampleRate);
        dtc->voices[i]->delayPitchTrackMode = dtc->delayPitchTrackMode;
        dtc->voices[i]->filterMode = dtc->filterMode;
        dtc->voices[i]->drive = dtc->drive;
        dtc->voices[i]->fmDepth3to2 = dtc->fmDepth3to2;
        dtc->voices[i]->fmDepth3to1 = dtc->fmDepth3to1;
        dtc->voices[i]->fmDepth2to1 = dtc->fmDepth2to1;
        dtc->voices[i]->syncEnable3to2 = dtc->syncEnable3to2;
        dtc->voices[i]->syncEnable3to1 = dtc->syncEnable3to1;
        dtc->voices[i]->syncEnable2to1 = dtc->syncEnable2to1;
        dtc->voices[i]->glideTimeMs = dtc->glideTimeMs;
        dtc->voices[i]->glideMode = dtc->glideMode;
        dtc->voices[i]->bitCrushBits = dtc->bitCrushBits;
        dtc->voices[i]->sampleReduceFactor = dtc->sampleReduceFactor;

        for (int oscIdx = 0; oscIdx < PolyLofiVoice::NUM_OSC; ++oscIdx) {
            dtc->voices[i]->setOscillatorParameters(oscIdx,
                dtc->oscWaveform[oscIdx],
                dtc->oscSemitone[oscIdx],
                dtc->oscFine[oscIdx],
                dtc->oscMorph[oscIdx],
                dtc->oscLevel[oscIdx]);
        }
    }

    // Initialize wavetable manager (allocates 3 DRAM buffers, sets up callbacks)
    dtc->wtManager.init(dramPtr);

    // Allocate allpass diffuser buffers (4-stage, one per voice)
    for (int i = 0; i < numVoices; ++i) {
        float* diffBuf = (float*)dramPtr;
        dramPtr += AllpassDiffuser::dramBytes();
        memset(diffBuf, 0, AllpassDiffuser::dramBytes());
        dtc->voices[i]->initDelayDiffuser(diffBuf);
    }

	return alg;
}

static void updateSyncedLfoSpeeds(_polyLofiAlgorithm_DTC* dtc);
static void updateSyncedDelayTime(_polyLofiAlgorithm_DTC* dtc);

#include "PolyLofiRouting.h"

void parameterChanged(_NT_algorithm* self, int p)
{
    _polyLofiAlgorithm* pThis = (_polyLofiAlgorithm*)self;
    _polyLofiAlgorithm_DTC* dtc = pThis->dtc;
    int16_t raw = pThis->v[p];

    // Filter subsystem
    if (p == kParamBaseCutoff || p == kParamResonance || p == kParamFilterEnvAmount ||
        p == kParamFilterMode || p == kParamFilterModel || p == kParamDrive || p == kParamFilterShape ||
        p == kParamKeyboardTracking) {
        routeFilter(dtc, p, raw);
        return;
    }

    // Oscillators (Waveform/Semi/Fine/Morph/Level for all 3)
    if ((p >= kParamOsc1Waveform && p <= kParamOsc1Level) ||
        (p >= kParamOsc2Waveform && p <= kParamOsc2Level) ||
        (p >= kParamOsc3Waveform && p <= kParamOsc3Level)) {
        routeOscillator(dtc, p, raw);
        return;
    }

    // Envelopes (Amp, Filter, Mod)
    if ((p >= kParamAmpAttack && p <= kParamAmpShape) ||
        (p >= kParamFilterAttack && p <= kParamFilterRelease) ||
        (p >= kParamModEnvAttack && p <= kParamModEnvShape)) {
        routeEnvelope(dtc, p, raw);
        return;
    }

    // Delay subsystem
    if (p == kParamDelayTime || p == kParamDelayFeedback || p == kParamDelayMix ||
        p == kParamDelayDiffusion || p == kParamDelayFBFilter || p == kParamDelayFBFreq ||
        p == kParamDelayPitchTrack || p == kParamDelaySyncMode) {
        routeDelay(dtc, self, p, raw);
        return;
    }

    // LFOs (Speed/Shape/Unipolar/Morph/SyncMode/KeySync for all 3)
    if ((p >= kParamLfoSpeed && p <= kParamLfo1KeySync) ||
        (p >= kParamLfo2Speed && p <= kParamLfo2KeySync) ||
        (p >= kParamLfo3Speed && p <= kParamLfo3KeySync)) {
        routeLfo(dtc, p, raw);
        return;
    }

    // Modulation matrix
    if (p >= kParamMod1Source && p <= kParamMod4Amount) {
        routeModMatrix(dtc, p, raw);
        return;
    }

    // FM / Sync
    if (p >= kParamFM3to2 && p <= kParamSync2to1) {
        routeFmSync(dtc, p, raw);
        return;
    }

    // Preset load/save
    if (p == kParamLoadPreset || p == kParamSavePreset || p == kParamSaveConfirm) {
        routePreset(dtc, self, p, raw);
        return;
    }

    // Everything else: MIDI channel, glide, bit crush, wavetable, pan spread
    routeMisc(dtc, p, raw);
}

// ---------------------------------------------------------------------------
// MIDI clock sync: update all synced LFO speeds from current BPM
// ---------------------------------------------------------------------------
static void updateSyncedLfoSpeeds(_polyLofiAlgorithm_DTC* dtc) {
    if (!dtc->clockTracker.isActive()) return;
    float qnHz = dtc->clockTracker.quarterNoteHz();
    for (int lfoIdx = 0; lfoIdx < 3; ++lfoIdx) {
        int mode = dtc->lfoSyncMode[lfoIdx];
        if (mode > 0 && mode < (int)(sizeof(kSyncMultipliers) / sizeof(float))) {
            float lfoHz = qnHz * kSyncMultipliers[mode];
            dtc->lfoSpeed[lfoIdx] = lfoHz;
            for (int i = 0; i < dtc->numVoices; ++i) {
                dtc->voices[i]->setLfoFrequency(lfoIdx, lfoHz);
                dtc->voices[i]->setLfoSampleHoldRate(lfoIdx, lfoHz);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// MIDI clock sync: update synced delay time from current BPM
// Delay time for division = (60 / BPM) / multiplier  in seconds.
// kSyncMultipliers converts quarter-note-Hz to LFO Hz, so
// delay_seconds = 1.0 / (quarterNoteHz * multiplier)
// ---------------------------------------------------------------------------
static void updateSyncedDelayTime(_polyLofiAlgorithm_DTC* dtc) {
    if (!dtc->clockTracker.isActive()) return;
    int mode = dtc->delaySyncMode;
    if (mode <= 0 || mode >= (int)(sizeof(kSyncMultipliers) / sizeof(float))) return;
    float qnHz = dtc->clockTracker.quarterNoteHz();
    float delaySec = 1.0f / (qnHz * kSyncMultipliers[mode]);
    // Clamp to delay buffer limit (~2.97s at 44.1 kHz)
    float maxSec = static_cast<float>(DELAY_SIZE - 1) / NT_globals.sampleRate;
    if (delaySec > maxSec) delaySec = maxSec;
    dtc->delaySamples = delaySec * NT_globals.sampleRate;
    for (int i = 0; i < dtc->numVoices; ++i) {
        dtc->voices[i]->delaySamples = dtc->delaySamples;
    }
}

// ---------------------------------------------------------------------------
// CV clock: update synced LFO/delay speeds from derived BPM
// ---------------------------------------------------------------------------
static void updateCvClockSpeeds(_polyLofiAlgorithm_DTC* dtc) {
    if (!dtc->cvClockActive || dtc->cvClockBpm <= 0.0f) return;
    float qnHz = dtc->cvClockBpm / 60.0f;
    for (int lfoIdx = 0; lfoIdx < 3; ++lfoIdx) {
        int mode = dtc->lfoSyncMode[lfoIdx];
        if (mode > 0 && mode < (int)(sizeof(kSyncMultipliers)/sizeof(float))) {
            float lfoHz = qnHz * kSyncMultipliers[mode];
            dtc->lfoSpeed[lfoIdx] = lfoHz;
            for (int v = 0; v < dtc->numVoices; ++v) {
                dtc->voices[v]->setLfoFrequency(lfoIdx, lfoHz);
                dtc->voices[v]->setLfoSampleHoldRate(lfoIdx, lfoHz);
            }
        }
    }
    if (dtc->delaySyncMode > 0) {
        int mode = dtc->delaySyncMode;
        if (mode > 0 && mode < (int)(sizeof(kSyncMultipliers)/sizeof(float))) {
            float delaySec = 1.0f / (qnHz * kSyncMultipliers[mode]);
            float maxSec = static_cast<float>(DELAY_SIZE - 1) / NT_globals.sampleRate;
            if (delaySec > maxSec) delaySec = maxSec;
            dtc->delaySamples = delaySec * NT_globals.sampleRate;
            for (int v = 0; v < dtc->numVoices; ++v)
                dtc->voices[v]->delaySamples = dtc->delaySamples;
        }
    }
}

// ---------------------------------------------------------------------------
// MIDI realtime callback: clock tracking
// ---------------------------------------------------------------------------
void midiRealtimeCb(_NT_algorithm* self, uint8_t byte)
{
    _polyLofiAlgorithm* pThis = (_polyLofiAlgorithm*)self;
    _polyLofiAlgorithm_DTC* dtc = pThis->dtc;

    if (dtc->clockTracker.onRealtimeByte(byte, static_cast<float>(NT_globals.sampleRate))) {
        updateSyncedLfoSpeeds(dtc);
        if (dtc->delaySyncMode > 0)
            updateSyncedDelayTime(dtc);
    }
}

void step( _NT_algorithm* self, float* busFrames, int numFramesBy4 )
{
    _polyLofiAlgorithm* pThis = (_polyLofiAlgorithm*)self;
    _polyLofiAlgorithm_DTC* dtc = pThis->dtc;
    
    // --- Wavetable: SD card mount detection + push loaded tables to voices ---
    dtc->wtManager.update(dtc->voices, dtc->numVoices);

    // --- Deferred Save confirm auto-reset (~150ms hold for display) ---
    if (dtc->saveResetCountdown > 0) {
        if (--dtc->saveResetCountdown == 0) {
            int32_t algIdx = NT_algorithmIndex(self);
            uint32_t offset = NT_parameterOffset();
            NT_setParameterFromAudio(algIdx, kParamSaveConfirm + offset, 0);
        }
    }

    int numFrames = numFramesBy4 * 4;

    // --- Hardware clock CV edge detection (1 PPQN assumed) ---
    if (pThis->v[kParamClockInput] > 0) {
        float* clockIn = busFrames + (pThis->v[kParamClockInput] - 1) * numFrames;
        const float threshold = 0.5f;
        for (int i = 0; i < numFrames; ++i) {
            float cv = clockIn[i];
            bool edge = (dtc->prevClockValue < threshold && cv >= threshold);
            dtc->prevClockValue = cv;
            if (edge) {
                if (dtc->cvClockActive && dtc->samplesSinceLastCvClock > 0) {
                    float periodSec = static_cast<float>(dtc->samplesSinceLastCvClock)
                                    / NT_globals.sampleRate;
                    dtc->cvClockBpm = 60.0f / periodSec;
                    updateCvClockSpeeds(dtc);
                }
                dtc->cvClockActive = true;
                dtc->samplesSinceLastCvClock = 0;
            }
            dtc->samplesSinceLastCvClock++;
        }
    } else {
        dtc->cvClockActive = false;
    }

    // --- Phase 3: Bus index validation (R6f) ---
    int leftBus = pThis->v[kParamOutput];
#if POLYLOFI_DEBUG
    if (leftBus < 1 || leftBus > 28) {
        ++dtc->dbgBusErrors;
        dtc->clockTracker.advance(numFrames);
        return;
    }
#endif
    float* outL = busFrames + ( leftBus - 1 ) * numFrames;
    bool replaceL = pThis->v[kParamOutputMode];

    int rightBus = pThis->v[kParamRightOutput];
    bool stereo = (rightBus > 0);
    float* outR = nullptr;
    bool replaceR = false;
    if (stereo) {
#if POLYLOFI_DEBUG
        if (rightBus < 1 || rightBus > 28) {
            ++dtc->dbgBusErrors;
            stereo = false;
        } else
#endif
        {
            outR = busFrames + ( rightBus - 1 ) * numFrames;
            replaceR = pThis->v[kParamRightOutputMode];
        }
    }

    float voiceBuf[MAX_BLOCK_SIZE];
    float mixL[MAX_BLOCK_SIZE];
    float mixR[MAX_BLOCK_SIZE];

    for (int i = 0; i < numFrames; ++i) mixL[i] = 0.0f;
    if (stereo) {
        for (int i = 0; i < numFrames; ++i) mixR[i] = 0.0f;
    }

    // --- Phase 1: Count active voices (R6f) ---
#if POLYLOFI_DEBUG
    int activeCount = 0;
#endif

    for (int v = 0; v < dtc->numVoices; ++v) {
        if (dtc->voices[v]->active) {
#if POLYLOFI_DEBUG
            ++activeCount;
#endif
            for (int i = 0; i < numFrames; ++i) voiceBuf[i] = 0.0f;
            dtc->voices[v]->processBlock(voiceBuf, numFrames, dtc->matrix);

            // Sum voice to internal mix buffer with per-voice stereo pan
            if (stereo) {
                float pL = dtc->voices[v]->panL;
                float pR = dtc->voices[v]->panR;
                for (int i = 0; i < numFrames; ++i) {
                    mixL[i] += voiceBuf[i] * pL;
                    mixR[i] += voiceBuf[i] * pR;
                }
            } else {
                for (int i = 0; i < numFrames; ++i) {
                    mixL[i] += voiceBuf[i];
                }
            }
        }
    }

    // --- Soft-clip and gain on internal mix, then write to bus ---
    {
        const float g = dtc->masterGain;
        if (replaceL) {
            for (int i = 0; i < numFrames; ++i)
                outL[i] = cheap_saturate(mixL[i]) * g;
        } else {
            for (int i = 0; i < numFrames; ++i)
                outL[i] += cheap_saturate(mixL[i]) * g;
        }
        if (stereo) {
            if (replaceR) {
                for (int i = 0; i < numFrames; ++i)
                    outR[i] = cheap_saturate(mixR[i]) * g;
            } else {
                for (int i = 0; i < numFrames; ++i)
                    outR[i] += cheap_saturate(mixR[i]) * g;
            }
        }
    }

#if POLYLOFI_DEBUG
    // --- Phase 1: Peak amplitude + voice count (R6f) ---
    dtc->dbgActiveVoices = static_cast<float>(activeCount);
    {
        float peak = 0.f;
        for (int i = 0; i < numFrames; ++i) {
            float a = outL[i] < 0.f ? -outL[i] : outL[i];
            if (a > peak) peak = a;
        }
        dtc->dbgPeakAmp = peak;
    }

    // --- Phase 2: NaN/Inf guard — clamp and count (R6f) ---
    for (int i = 0; i < numFrames; ++i) {
        if (!std::isfinite(outL[i])) { outL[i] = 0.f; ++dtc->dbgNanCount; }
    }
    if (stereo) {
        for (int i = 0; i < numFrames; ++i) {
            if (!std::isfinite(outR[i])) { outR[i] = 0.f; ++dtc->dbgNanCount; }
        }
    }

    ++dtc->dbgFrameCounter;
#endif

    dtc->clockTracker.advance(numFrames);
}

void midiMessage(_NT_algorithm* self, uint8_t status, uint8_t data1, uint8_t data2)
{
    _polyLofiAlgorithm* pThis = (_polyLofiAlgorithm*)self;
    _polyLofiAlgorithm_DTC* dtc = pThis->dtc;

    int channel = (status & 0x0F) + 1; // 1-16
    if (dtc->midiChannel != 0 && channel != dtc->midiChannel) return;

    if ((status & 0xF0) == 0x90 && data2 > 0) { // Note on
        int note = data1;
        float vel = data2 / 127.0f;
#if POLYLOFI_DEBUG
        dtc->dbgLastNote = static_cast<float>(note);
#endif
        if (dtc->legato) {
            // Legato mono: always use voice 0
            if (dtc->voices[0]->active && dtc->voices[0]->isAmpGated())
                dtc->voices[0]->legatoRetrigger(note, vel);
            else
                dtc->voices[0]->noteOn(note, vel);
        } else {
            auto alloc = VoiceAllocator::allocate(dtc->voices, dtc->numVoices, note);
            if (alloc.stolen)
                dtc->voices[alloc.index]->stealVoice(note, vel);
            else
                dtc->voices[alloc.index]->noteOn(note, vel);
        }
        
    } else if ((status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && data2 == 0)) { // Note off
        int note = data1;
        if (dtc->legato) {
            // Legato mono: only release if it matches the current note on voice 0
            if (dtc->voices[0]->note == note && dtc->voices[0]->active)
                dtc->voices[0]->noteOff();
        } else {
            for (int i = 0; i < dtc->numVoices; ++i) {
                if (dtc->voices[i]->note == note && dtc->voices[i]->active) {
                    dtc->voices[i]->noteOff();
                    break;
                }
            }
        }
    } else if ((status & 0xF0) == 0xE0) { // Pitch bend
        // data1 = LSB, data2 = MSB → 14-bit value 0-16383, center 8192
        int bendRaw = (data2 << 7) | data1;
        float bendNorm = (static_cast<float>(bendRaw) - 8192.0f) / 8192.0f; // -1.0 to +1.0
        dtc->pitchBendSemitones = bendNorm * dtc->pitchBendRange;
        for (int i = 0; i < dtc->numVoices; ++i) {
            dtc->voices[i]->setPitchBend(dtc->pitchBendSemitones);
        }
    } else if ((status & 0xF0) == 0xD0) { // Channel Aftertouch
        dtc->aftertouchValue = static_cast<float>(data1) / 127.0f;
        for (int i = 0; i < dtc->numVoices; ++i) {
            dtc->voices[i]->setAftertouch(dtc->aftertouchValue);
        }
    } else if ((status & 0xF0) == 0xA0) { // Poly Aftertouch
        uint8_t touchNote = data1;
        float touchVal = static_cast<float>(data2) / 127.0f;
        for (int i = 0; i < dtc->numVoices; ++i) {
            if (dtc->voices[i]->note == touchNote && dtc->voices[i]->active) {
                dtc->voices[i]->setAftertouch(touchVal);
            }
        }
    } else if ((status & 0xF0) == 0xB0) { // Control Change
        uint8_t cc = data1;
        uint8_t val = data2;
        if (cc == 1) { // Mod Wheel
            dtc->modWheelValue = static_cast<float>(val) / 127.0f;
            for (int i = 0; i < dtc->numVoices; ++i) {
                dtc->voices[i]->setModWheel(dtc->modWheelValue);
            }
        } else if (cc == 64) { // Sustain Pedal
            bool down = (val >= 64);
            dtc->sustainPedalDown = down;
            for (int i = 0; i < dtc->numVoices; ++i) {
                dtc->voices[i]->setSustainPedal(down);
            }
        }
    }
}

bool draw( _NT_algorithm* self )
{
#if POLYLOFI_DEBUG
    _polyLofiAlgorithm* pThis = (_polyLofiAlgorithm*)self;
    _polyLofiAlgorithm_DTC* dtc = pThis->dtc;
    char buf[16];

    // Row 1: Voice count + Peak amplitude
    NT_drawText(0, 48, "V:");
    NT_intToString(buf, static_cast<int32_t>(dtc->dbgActiveVoices));
    NT_drawText(16, 48, buf);

    NT_drawText(36, 48, "Pk:");
    NT_floatToString(buf, dtc->dbgPeakAmp, 3);
    NT_drawText(58, 48, buf);

    // Row 2: Last note + Frame counter + NaN count + Bus errors
    NT_drawText(0, 56, "N:");
    NT_intToString(buf, static_cast<int32_t>(dtc->dbgLastNote));
    NT_drawText(14, 56, buf);

    NT_drawText(42, 56, "F:");
    NT_intToString(buf, static_cast<int32_t>(dtc->dbgFrameCounter & 0xFFFF));
    NT_drawText(54, 56, buf);

    if (dtc->dbgNanCount > 0) {
        NT_drawText(100, 56, "NaN:");
        NT_intToString(buf, static_cast<int32_t>(dtc->dbgNanCount));
        NT_drawText(128, 56, buf);
    }

    if (dtc->dbgBusErrors > 0) {
        NT_drawText(170, 56, "BUS!");
    }

    return true;  // request screen redraw
#else
    (void)self;
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Test helper: inject wavetable data directly into all voices for an oscillator.
// Used by headless tests to bypass SD card loading.
// ---------------------------------------------------------------------------
extern "C" void polyLofi_injectWavetable(_NT_algorithm* self, int oscIdx,
                                          const int16_t* data, uint32_t numWaves,
                                          uint32_t waveLength) {
    _polyLofiAlgorithm* pThis = (_polyLofiAlgorithm*)self;
    _polyLofiAlgorithm_DTC* dtc = pThis->dtc;
    dtc->wtManager.inject(oscIdx, data, numWaves, waveLength, dtc->voices, dtc->numVoices);
}

// ---------------------------------------------------------------------------
// parameterString — provide display names for kNT_unitHasStrings parameters.
// Currently handles wavetable selection: shows the wavetable name from the
// SD card filesystem, following the same pattern as samplePlayer.cpp.
// ---------------------------------------------------------------------------
int parameterString(_NT_algorithm* self, int p, int v, char* buff)
{
    int len = 0;
    switch (p)
    {
    case kParamOsc1Wavetable:
    case kParamOsc2Wavetable:
    case kParamOsc3Wavetable:
    {
        _NT_wavetableInfo info;
        NT_getWavetableInfo(static_cast<uint32_t>(v), info);
        if (info.name) {
            strncpy(buff, info.name, kNT_parameterStringSize - 1);
            buff[kNT_parameterStringSize - 1] = '\0';
            len = strlen(buff);
        }
        break;
    }
    case kParamLoadPreset:
    case kParamSavePreset:
    {
        _polyLofiAlgorithm* pThis = (_polyLofiAlgorithm*)self;
        if (v >= 0 && v < kNumPresets) {
            strncpy(buff, pThis->dtc->presets.slots[v].name, kNT_parameterStringSize - 1);
            buff[kNT_parameterStringSize - 1] = '\0';
            len = strlen(buff);
        }
        break;
    }
    }
    return len;
}

// ---------------------------------------------------------------------------
// serialise — write preset bank to JSON
// ---------------------------------------------------------------------------
void polyLofi_serialise(_NT_algorithm* self, _NT_jsonStream& stream)
{
    _polyLofiAlgorithm* pThis = (_polyLofiAlgorithm*)self;
    PresetBank& bank = pThis->dtc->presets;

    stream.addMemberName("presets");
    stream.openArray();
    for (int s = 0; s < kNumPresets; ++s) {
        stream.openObject();
        stream.addMemberName("name");
        stream.addString(bank.slots[s].name);
        stream.addMemberName("occupied");
        stream.addBoolean(bank.slots[s].occupied);
        if (bank.slots[s].occupied) {
            stream.addMemberName("params");
            stream.openObject();
            for (int i = 0; i < kNumSynthParams; ++i) {
                stream.addMemberName(self->parameters[i].name);
                stream.addNumber((int)bank.slots[s].values[i]);
            }
            stream.closeObject();
        }
        stream.closeObject();
    }
    stream.closeArray();
}

// ---------------------------------------------------------------------------
// deserialise — read preset bank from JSON
// ---------------------------------------------------------------------------
bool polyLofi_deserialise(_NT_algorithm* self, _NT_jsonParse& parse)
{
    _polyLofiAlgorithm* pThis = (_polyLofiAlgorithm*)self;
    PresetBank& bank = pThis->dtc->presets;

    int numMembers;
    if (!parse.numberOfObjectMembers(numMembers))
        return false;

    for (int m = 0; m < numMembers; ++m) {
        if (parse.matchName("presets")) {
            int numSlots;
            if (!parse.numberOfArrayElements(numSlots))
                return false;
            for (int s = 0; s < numSlots && s < kNumPresets; ++s) {
                int numObj;
                if (!parse.numberOfObjectMembers(numObj))
                    return false;
                for (int o = 0; o < numObj; ++o) {
                    if (parse.matchName("name")) {
                        const char* str = nullptr;
                        if (!parse.string(str))
                            return false;
                        if (str) {
                            strncpy(bank.slots[s].name, str,
                                    sizeof(bank.slots[s].name) - 1);
                            bank.slots[s].name[sizeof(bank.slots[s].name) - 1] = '\0';
                        }
                    } else if (parse.matchName("occupied")) {
                        bool occ = false;
                        if (!parse.boolean(occ))
                            return false;
                        bank.slots[s].occupied = occ;
                    } else if (parse.matchName("params")) {
                        int numParams;
                        if (!parse.numberOfObjectMembers(numParams))
                            return false;
                        for (int p = 0; p < numParams; ++p) {
                            // Match by name against known parameters
                            bool matched = false;
                            for (int i = 0; i < kNumSynthParams; ++i) {
                                if (parse.matchName(self->parameters[i].name)) {
                                    int val = 0;
                                    if (!parse.number(val))
                                        return false;
                                    bank.slots[s].values[i] = (int16_t)val;
                                    matched = true;
                                    break;
                                }
                            }
                            if (!matched) {
                                if (!parse.skipMember())
                                    return false;
                            }
                        }
                    } else {
                        if (!parse.skipMember())
                            return false;
                    }
                }
            }
            // Skip extra slots beyond kNumPresets
            for (int s = kNumPresets; s < numSlots; ++s) {
                if (!parse.skipMember())
                    return false;
            }
        } else {
            if (!parse.skipMember())
                return false;
        }
    }
    return true;
}

static const _NT_factory factory = 
{
    NT_MULTICHAR( 'P', 'o', 'l', 'y' ),          // guid
    "Poly Lofi",                                    // name
    "Polyphonic lo-fi oscillator with envelopes and filter",  // description
    ARRAY_SIZE(specs),                              // numSpecifications
    specs,                                          // specifications
    NULL,                                           // calculateStaticRequirements
    NULL,                                           // initialise
    calculateRequirements,                          // calculateRequirements
    construct,                                      // construct
    parameterChanged,                               // parameterChanged
    step,                                           // step
    draw,                                           // draw
    midiRealtimeCb,                                // midiRealtime
    midiMessage,                                    // midiMessage
    0,                                              // tags
    NULL,                                           // hasCustomUi
    NULL,                                           // customUi
    NULL,                                           // setupUi
    polyLofi_serialise,                             // serialise
    polyLofi_deserialise,                           // deserialise
    NULL,                                           // midiSysEx
    NULL,                                           // parameterUiPrefix
    parameterString                                 // parameterString
};

uintptr_t pluginEntry( _NT_selector selector, uint32_t data )
{
	switch ( selector )
	{
	case kNT_selector_version:
		return kNT_apiVersionCurrent;
	case kNT_selector_numFactories:
		return 1;
	case kNT_selector_factoryInfo:
		return (uintptr_t)( ( data == 0 ) ? &factory : NULL );
	}
	return 0;
}