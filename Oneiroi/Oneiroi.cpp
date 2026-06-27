// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Philippe Lamarche — Disting NT adaptation of Befaco Oneiroi
//
// This file is the NT glue layer that maps the Oneiroi DSP engine
// (Befaco / Roberto Noris) and OWL platform utilities (Rebel Technology)
// onto the Expert Sleepers Disting NT plugin API.
//
// See NOTICE for full upstream attribution and LICENSE for terms.

#include <math.h>
#include <new>
#include <distingnt/api.h>
#include <distingnt/wav.h>
#include <Oneiroi/Oneiroi.h>
#include <Oneiroi/SampleBuffer.hpp>



enum
{
    kParamLeftOutput,
    kParamLeftOutputMode,
    kParamRightOutput,
    kParamRightOutputMode,
    kParamLeftInput,
    kParamRightInput,
    kParamClockInput,
    kParamPitchInput,

    kParamInputLevel, 
    kParamOutputLevel,

    kParamOscSemi,
    kParamOscFine,
    kParamOscV8c,
    kParamOscDetune,
    kParamOscPitchModAmount, 
    kParamOscUnison,
    kParamOscDetuneModAmount,
    kParamSinOscVol,
    kParamSSOscVol,
    kParamSSWT,
    
	kParamfilterVol,
    kParamfilterMode,
    kParamfilterCutoff,
    kParamfilterCutoffModAmount,
    kParamfilterResonance,
    kParamfilterResonanceModAmount,
    kParamfilterPosition,

	kParamlooperVol,
    kParamlooperSos,
    kParamlooperFilter,
    kParamlooperSpeed,
    kParamlooperSpeedModAmount,
    kParamlooperStart,
    kParamlooperStartModAmount,
    kParamlooperLength,
    kParamlooperLengthModAmount,
    kParamlooperRecording,
    kParamlooperResampling,
	kParamlooperClear,

    kParamresonatorVol,
    kParamresonatorTune,
    kParamresonatorFeedback,
    kParamresonatorDissonance,

    kParamechoVol,
    kParamechoDensity,
    kParamechoRepeats,
    kParamechoFilter,

    kParamambienceVol,
    kParamambienceDecay,
    kParamambienceSpacetime,
    kParamambienceAutoPan,

    kParammodType,
    kParammodSpeed,
    kParammodLevel,
	kParamActionRandomize,
    kParamActionUndo,
    kParamActionRedo,
    kParamRandScope,
    kParamLooperFolder,
    kParamLooperFile,
};

// Define the range of parameters to randomize/undo (skipping Inputs/Outputs/Actions)
#define PARAM_RND_START kParamOscSemi 
#define PARAM_RND_END   kParammodLevel
#define NUM_UNDOABLE_PARAMS (PARAM_RND_END - PARAM_RND_START + 1)
#define UNDO_STACK_SIZE 8

struct _OneiroiAlgorithm_DTC
{
    PatchCtrls patchCtrls;
    PatchCvs patchCvs;
    PatchState patchState;
	Oneiroi* Oneiroi_;
	AudioBuffer* buffer;
	float semi= 0.f, fine=0.f, v8c = 0.f, prevClockValue = 0.f, pitchInput=0.f;
	uint8_t dtcMemory[_allocatableDTCMemorySize];
	int16_t undoStack[UNDO_STACK_SIZE][NUM_UNDOABLE_PARAMS];
    // Invariant: undoStack[undoCursor] always matches the live parameter values.
    // undoCursor == -1 means no history exists yet.
    int     undoCount  = 0;   // number of valid snapshots (0..UNDO_STACK_SIZE)
    int     undoCursor = -1;  // index of the currently applied snapshot

    // Looper file loading
    _NT_wavRequest wavReq     = {};    // must persist until callback fires
    bool     wavLoading       = false;
    uint32_t wavFileFrames    = 0;     // frames requested (known before callback)
    bool     wavPendingSet    = false; // step() reads this to apply length param
    int16_t  wavPendingLen    = 0;     // pending kParamlooperLength value
    int      looperFolder     = 0;
    int      looperFile       = 0;
};

struct _OneiroiAlgorithm : public _NT_algorithm
{
	_OneiroiAlgorithm( _OneiroiAlgorithm_DTC* dtc_ ) : dtc( dtc_ ) {}
	~_OneiroiAlgorithm() {}
	
	_OneiroiAlgorithm_DTC*	dtc;
};

// Enum strings for the trigger buttons
static char const * const enumStringsTrigger[] = { "Idle", "Trigger" };

static char const * const enumStringsFiltermode[] = {
	 "Low-pass", "Band-pass", "High-pass", "Comb filter"
};

static char const * const enumStringsOnOff[] = {
	 "Off", "On"
};

static char const * const enumStringsFilterPos[] = {
	 "After oscs", "Res <-> Echo", "Echo <-> Amb", "End of chain"
};

static char const * const enumStringsSSWT[] = {
	 "SuperSaw", "WaveTable"
};

static const char * const enumStringsRandScope[] = {
    "All", "Oscillator", "Filter", "Looper", "Resonator", "Echo", "Ambience", "Mod", nullptr
};

static const _NT_parameter	parameters[] = {
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Left Output", 1, 13 )
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Right Output", 1, 14 )
    NT_PARAMETER_AUDIO_INPUT("Left Input (mono)", 0, 0)
    NT_PARAMETER_AUDIO_INPUT("Right Input", 0, 0)
	NT_PARAMETER_CV_INPUT("Clock Input", 0, 0)
	NT_PARAMETER_CV_INPUT("Pitch Input", 0, 0)

    // --- Add new parameters here ---
    { .name = "Input Level", .min = 0, .max = 1000, .def = 700, .unit = kNT_unitDb, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Output Level", .min = 0, .max = 1000, .def = 700, .unit = kNT_unitDb, .scaling = kNT_scaling1000, .enumStrings = NULL },

    { .name = "Semi", .min = -48, .max = 48, .def = 0, .unit = kNT_unitSemitones, .scaling = 0, .enumStrings = NULL },
	{ .name = "Fine", .min =-50 , .max = 50, .def = 0, .unit = kNT_unitCents, .scaling = 0, .enumStrings = NULL },
	{ .name = "Volt/Octave", .min = -5000, .max = 5000, .def = 0, .unit = kNT_unitVolts, .scaling = kNT_scaling1000, .enumStrings = NULL },
	{ .name = "Detune", .min = -1000, .max = 1000, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Pitch mod amount", .min = -1000, .max = 1000, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
	{ .name = "Unison", .min = -1000, .max = 1000, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
	{ .name = "Detune mod amount", .min = -1000, .max = 1000, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Sine Vol", .min = 0, .max = 1000, .def = 750, .unit = kNT_unitDb, .scaling = kNT_scaling1000, .enumStrings = NULL },
	{ .name = "SS/WT Vol", .min = 0, .max = 1000, .def = 750, .unit = kNT_unitDb, .scaling = kNT_scaling1000, .enumStrings = NULL },
	{ .name = "SS/WT Switch", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsSSWT },
	
	{ .name = "Vol", .min = 0, .max = 1000, .def = 750, .unit = kNT_unitDb, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Mode", .min = 0, .max = 3, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsFiltermode },
    { .name = "Cutoff", .min = 0, .max = 22000, .def = 22000, .unit = kNT_unitHz, .scaling = 0, .enumStrings = NULL },
    { .name = "Cutoff Mod",  .min = -1000, .max = 1000, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Resonance", .min = 0, .max = 1000, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Res Mod",  .min = -1000, .max = 1000, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Position", .min = 0, .max = 3, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsFilterPos },
	
	{ .name = "Vol", .min = 0, .max = 1000, .def = 750, .unit = kNT_unitDb, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Sound on Sound", .min = 0, .max = 1000, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "DJ Filter", .min = 0, .max = 1000, .def = 550, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL }, 
    { .name = "Speed", .min = -2000, .max = 2000, .def = 1000, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "SpeedModAmount",  .min = -1000, .max = 1000, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Start Position", .min = 0, .max = 1000, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "StartModAmount",  .min = -1000, .max = 1000, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Loop Length", .min = 0, .max = 1000, .def = 1000, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "LengthModAmount", .min = -1000, .max = 1000, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Recording", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOnOff },
    { .name = "Resampling", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOnOff },
  	{ .name = "Clear loop", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOnOff },


    { .name = "Resonator Vol", .min = 0, .max = 1000, .def = 500, .unit = kNT_unitDb, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Resonator Tune", .min = -1200, .max = 1200, .def = 0, .unit = kNT_unitCents, .scaling = 0, .enumStrings = NULL },
    { .name = "Resonator Feedback", .min = 0, .max = 1000, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Resonator Dissonance", .min = 0, .max = 1000, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },

    { .name = "Echo Vol", .min = 0, .max = 1000, .def = 250, .unit = kNT_unitDb, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Echo Density", .min = 0, .max = 1000, .def = 500, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Echo Repeats", .min = 0, .max = 1000, .def = 500, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Echo DJ Filter", .min = 0, .max = 1000, .def = 550, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },

    { .name = "Ambience Vol", .min = 0, .max = 1000, .def = 175, .unit = kNT_unitDb, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Ambience Decay", .min = 0, .max = 1000, .def = 500, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Ambience Spacetime", .min = 0, .max = 1000, .def = 500, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Ambience Auto Pan", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOnOff },

    // Modulation
    { .name = "Mod Type", .min = 0, .max = 800, .def = 0, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Mod Speed", .min = 0, .max = 1000, .def = 500, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Mod Level", .min = 0, .max = 1000, .def = 250, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },

	{ .name = "Randomize",  .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsTrigger },
    { .name = "Undo",       .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsTrigger },
    { .name = "Redo",       .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsTrigger },
    { .name = "Rand Scope", .min = 0, .max = 7, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsRandScope },
    { .name = "Looper Folder", .min = 0, .max = 255, .def = 0, .unit = kNT_unitHasStrings, .scaling = 0, .enumStrings = NULL },
    { .name = "Looper File",   .min = 0, .max = 255, .def = 0, .unit = kNT_unitConfirm,    .scaling = 0, .enumStrings = NULL },
};


static const uint8_t page1[] = { kParamOscSemi,	kParamOscFine,kParamOscV8c, kParamOscDetune, kParamOscPitchModAmount, kParamOscUnison, kParamOscDetuneModAmount, kParamSSOscVol, kParamSinOscVol, kParamSSWT};
static const uint8_t page2[] = {kParamfilterMode, kParamfilterCutoff, kParamfilterCutoffModAmount, kParamfilterResonance, kParamfilterResonanceModAmount, kParamfilterPosition, kParamfilterVol};
static const uint8_t page3[] = { kParamlooperVol, kParamlooperSos, kParamlooperFilter, kParamlooperSpeed, kParamlooperSpeedModAmount, kParamlooperStart, kParamlooperStartModAmount, kParamlooperLength, kParamlooperLengthModAmount, kParamlooperRecording, kParamlooperResampling, kParamlooperClear, kParamLooperFolder, kParamLooperFile};
static const uint8_t page4[] = { kParamresonatorVol, kParamresonatorTune, kParamresonatorFeedback, kParamresonatorDissonance};
static const uint8_t page5[] = { kParamechoVol, kParamechoDensity, kParamechoRepeats, kParamechoFilter };
static const uint8_t page6[] = { kParamambienceVol, kParamambienceDecay, kParamambienceSpacetime, kParamambienceAutoPan };
static const uint8_t page7[] = { kParammodType, kParammodSpeed, kParammodLevel};
static const uint8_t page8[] = { kParamRandScope, kParamActionRandomize, kParamActionUndo, kParamActionRedo};
static const uint8_t page9[] = { kParamLeftOutput, kParamLeftOutputMode, kParamRightOutput, kParamRightOutputMode, kParamLeftInput, kParamRightInput, kParamClockInput, kParamPitchInput, kParamInputLevel, kParamOutputLevel };



static const _NT_parameterPage pages[] = {
	{ .name = "Oscillators", .numParams = ARRAY_SIZE(page1), .params = page1 },
	{ .name = "Filters", .numParams = ARRAY_SIZE(page2), .params = page2 },
	{ .name = "Looper", .numParams = ARRAY_SIZE(page3), .params = page3 },
	{ .name = "Resonator", .numParams = ARRAY_SIZE(page4), .params = page4 },
	{ .name = "Echo", .numParams = ARRAY_SIZE(page5), .params = page5 },
	{ .name = "Ambience", .numParams = ARRAY_SIZE(page6), .params = page6 },
    { .name = "Modulation", .numParams = ARRAY_SIZE(page7), .params = page7 },
	{ .name = "Actions", .numParams = ARRAY_SIZE(page8), .params = page8 },
	{ .name = "Routing", .numParams = ARRAY_SIZE(page9), .params = page9 },
};

static const _NT_parameterPages parameterPages = {
	.numPages = ARRAY_SIZE(pages),
	.pages = pages,
};


// xorshift32 RNG — avoids stdlib rand() which is always seeded with 1 on bare metal.
// The state advances across calls so each press of Randomize gives a different result.
static uint32_t _ntRandState = 1337u;
static inline uint32_t ntRand() {
    _ntRandState ^= _ntRandState << 13;
    _ntRandState ^= _ntRandState >> 17;
    _ntRandState ^= _ntRandState << 5;
    return _ntRandState;
}

// Randomization scope: which section of parameters to randomize.
// Scope 0 (All) randomizes everything; scopes 1–7 target a single section.
static const uint8_t rndGroupOsc[]      = { kParamOscSemi, kParamOscFine, kParamOscV8c, kParamOscDetune, kParamOscPitchModAmount, kParamOscUnison, kParamOscDetuneModAmount, kParamSinOscVol, kParamSSOscVol, kParamSSWT };
static const uint8_t rndGroupFilter[]   = { kParamfilterVol, kParamfilterMode, kParamfilterCutoff, kParamfilterCutoffModAmount, kParamfilterResonance, kParamfilterResonanceModAmount, kParamfilterPosition };
static const uint8_t rndGroupLooper[]   = { kParamlooperVol, kParamlooperSos, kParamlooperFilter, kParamlooperSpeed, kParamlooperSpeedModAmount, kParamlooperStart, kParamlooperStartModAmount, kParamlooperLength, kParamlooperLengthModAmount };
static const uint8_t rndGroupResonator[] = { kParamresonatorVol, kParamresonatorTune, kParamresonatorFeedback, kParamresonatorDissonance };
static const uint8_t rndGroupEcho[]     = { kParamechoVol, kParamechoDensity, kParamechoRepeats, kParamechoFilter };
static const uint8_t rndGroupAmbience[] = { kParamambienceVol, kParamambienceDecay, kParamambienceSpacetime, kParamambienceAutoPan };
static const uint8_t rndGroupMod[]      = { kParammodType, kParammodSpeed, kParammodLevel };

struct _RndGroup { const uint8_t* params; int count; };
static const _RndGroup rndGroups[] = {
    { nullptr,          0  },  // 0: All
    { rndGroupOsc,      10 },  // 1: Oscillator
    { rndGroupFilter,    7 },  // 2: Filter
    { rndGroupLooper,    9 },  // 3: Looper
    { rndGroupResonator, 4 },  // 4: Resonator
    { rndGroupEcho,      4 },  // 5: Echo
    { rndGroupAmbience,  4 },  // 6: Ambience
    { rndGroupMod,       3 },  // 7: Mod
};

// Returns true if paramIdx should be randomized for the given scope.
// Always excludes looper operational toggles regardless of scope.
static inline bool shouldRandomize(int paramIdx, int scope) {
    if (paramIdx == kParamlooperRecording ||
        paramIdx == kParamlooperResampling ||
        paramIdx == kParamlooperClear) return false;
    if (scope <= 0) return true; // All
    const _RndGroup& g = rndGroups[scope];
    for (int j = 0; j < g.count; ++j)
        if (g.params[j] == (uint8_t)paramIdx) return true;
    return false;
}

static inline int16_t ntGetRandomValue(const _NT_parameter& p) {
    int32_t range = (int32_t)p.max - (int32_t)p.min + 1;
    return (int16_t)((int32_t)p.min + (int32_t)(ntRand() % (uint32_t)range));
}

// Apply the snapshot at undoStack[idx] back to the live parameters.
// Uses NT_setParameterFromAudio as required when called from parameterChanged().
static void applySnapshot(_OneiroiAlgorithm* self, int idx) {
    int32_t algoIndex = NT_algorithmIndex(self);
    for (int i = 0; i < NUM_UNDOABLE_PARAMS; ++i) {
        int paramIdx = PARAM_RND_START + i;
        int16_t stored = self->dtc->undoStack[idx][i];
        if (self->v[paramIdx] != stored)
            NT_setParameterFromAudio(algoIndex, paramIdx + NT_parameterOffset(), stored);
    }
}

// Save self->v[] into a new snapshot at the top of the ring buffer.
// Evicts the oldest entry when the buffer is full.
// Returns the index where the snapshot was written.
static int pushSnapshot(_OneiroiAlgorithm* self) {
    _OneiroiAlgorithm_DTC* dtc = self->dtc;
    if (dtc->undoCount == UNDO_STACK_SIZE) {
        memmove(dtc->undoStack[0], dtc->undoStack[1],
                (UNDO_STACK_SIZE - 1) * sizeof(dtc->undoStack[0]));
        dtc->undoCount--;
        if (dtc->undoCursor > 0) dtc->undoCursor--;
    }
    int writeIdx = dtc->undoCount;
    for (int i = 0; i < NUM_UNDOABLE_PARAMS; ++i)
        dtc->undoStack[writeIdx][i] = self->v[PARAM_RND_START + i];
    dtc->undoCount++;
    return writeIdx;
}

// ---------------------------------------------------------------------------
// Looper file load callback
// Called from the SD card thread after NT_readSampleFrames() completes.
// All heavy work (tiling, copy) is done here so step() only sets one param.
// ---------------------------------------------------------------------------
static void wavLoadCallback(void* data, bool success) {
    auto* dtc = static_cast<_OneiroiAlgorithm_DTC*>(data);
    dtc->wavLoading = false;
    if (!success || dtc->wavFileFrames == 0) return;

    FloatArray* buf = dtc->Oneiroi_->GetLooperFloatArray();
    float* bufData  = buf->getData();
    uint32_t fileFrames = dtc->wavFileFrames;

    // Tile L channel to fill the full channel buffer if file is shorter than ~5.46 s.
    if (fileFrames < (uint32_t)kLooperChannelBufferLength) {
        uint32_t pos = fileFrames;
        while (pos < (uint32_t)kLooperChannelBufferLength) {
            uint32_t toCopy = (uint32_t)kLooperChannelBufferLength - pos;
            if (toCopy > fileFrames) toCopy = fileFrames;
            memcpy(bufData + pos, bufData, toCopy * sizeof(float));
            pos += toCopy;
        }
    }

    // Mirror L to R so the looper plays the file on both channels.
    memcpy(bufData + kLooperChannelBufferLength, bufData, kLooperChannelBufferLength * sizeof(float));

    // Compute the looper length parameter value that best matches the file duration.
    // lengthLUT_ maps 0..1 → kLooperLoopLengthMin..kLooperChannelBufferLength using x^3 (expo y=3).
    // Inverse: pos = cbrt((fileSamples - min) / (max - min))
    const float fmin = (float)kLooperLoopLengthMin;
    const float fmax = (float)kLooperChannelBufferLength;
    float t = ((float)fileFrames - fmin) / (fmax - fmin);
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    int16_t pval = (int16_t)(cbrtf(t) * 1000.f + 0.5f);
    if (pval < 0) pval = 0;
    if (pval > 1000) pval = 1000;

    // Defer the parameter set to step() — NT_setParameterFromAudio cannot be
    // called safely from an SD card callback thread.
    dtc->wavPendingLen = pval;
    dtc->wavPendingSet = true;
}

void calculateRequirements( _NT_algorithmRequirements& req, const int32_t* specifications )
{
	req.numParameters = ARRAY_SIZE(parameters);
	req.sram = sizeof(_OneiroiAlgorithm);
	req.dram = _allocatableMemorySize;
	req.dtc = sizeof(_OneiroiAlgorithm_DTC);
	req.itc = 0;
}

_NT_algorithm*	construct( const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req, const int32_t* specifications )
{
	_OneiroiAlgorithm_DTC* dtc = new (ptrs.dtc) _OneiroiAlgorithm_DTC();
	_OneiroiAlgorithm* alg = new (ptrs.sram) _OneiroiAlgorithm( (_OneiroiAlgorithm_DTC*)ptrs.dtc );
	_allocatedMemory = 0;
	_allocatedDTCMemory = 0;
	_allocatableMemory = ptrs.dram;
	_allocatableDTCMemory = &(dtc->dtcMemory[0]);
    alg->dtc->patchState.sampleRate = NT_globals.sampleRate;
	alg->dtc->patchState.blockSize =  NT_globals.maxFramesPerStep;
	alg->dtc->patchState.blockRate =  NT_globals.sampleRate/NT_globals.maxFramesPerStep;
	alg->dtc->buffer = NTSampleBuffer::create(2, NT_globals.maxFramesPerStep);
	auto & pc = alg->dtc->patchCtrls;
	pc.oscPitch = 261.63f;
	pc.osc2Vol = 0.9f;
	alg->dtc->patchState.outLevel = 5.f;
	
	
	pc.filterCutoff = 20000.f;
	pc.filterMode = 0;
	pc.filterResonance = 0.f;
	pc.filterResonanceModAmount = 0.f;
    pc.filterResonanceCvAmount = 0.f;
	pc.filterCutoffCvAmount=0.f;
	pc.filterCutoffModAmount=0.f;
	pc.looperLengthCvAmount = 0.f;
	pc.looperLengthModAmount = 0.f;
	pc.looperSpeedCvAmount = 0.f;
	pc.looperSpeedModAmount = 0.f;
	pc.looperStartCvAmount = 0.f;
	pc.looperStartModAmount = 0.f;
    alg->dtc->patchCvs.filterResonance = 0.f;
	alg->dtc->patchCvs.filterCutoff = 0.f;
	
	alg->dtc->Oneiroi_ = Oneiroi::create(&alg->dtc->patchCtrls, &alg->dtc->patchCvs, &alg->dtc->patchState);
	alg->parameters = parameters;
	alg->parameterPages = &parameterPages;
	alg->dtc->patchState.startupPhase = StartupPhase::STARTUP_DONE;
	alg->dtc->patchState.modValue=0.f;
	alg->dtc->patchState.modAttenuverters= false; 
	alg->dtc->patchState.cvAttenuverters = false;

	
	pc.looperSos = 0.f;
	pc.looperFilter = 0.55f; // Center is not 0.5
	pc.looperResampling = 0.f;
	pc.oscUseWavetable = 0.f;
	//unison_ = 0.55f; // Center is not 0.5 REVIEW!
	pc.filterMode = 0.f;
	pc.filterPosition = 0.f;
	pc.modType = 0.f;
	pc.resonatorDissonance = 0.f;
	pc.echoFilter = 0.55f; // Center is not 0.5
	pc.ambienceAutoPan = 0.f;

	// Modulation
	pc.looperLengthModAmount = 0.f;
	pc.looperSpeedModAmount = 0.f;
	pc.looperStartModAmount = 0.f;
	pc.oscDetuneModAmount = 0.f;
	pc.oscPitchModAmount = 0.f;
	pc.filterCutoffModAmount = 0.5f;
	pc.filterResonanceModAmount = 0.f;
	pc.resonatorTuneModAmount = 0.f;
	pc.resonatorFeedbackModAmount = 0.f;
	pc.echoDensityModAmount = 0.f;
	pc.echoRepeatsModAmount = 0.f;
	pc.ambienceDecayModAmount = 0.f;
	pc.ambienceSpacetimeModAmount = 0.f;

	// CVs
	pc.looperSpeedCvAmount = 1.f;
	pc.looperStartCvAmount = 1.f;
	pc.looperLengthCvAmount = 1.f;
	pc.oscPitchCvAmount = 1.f;
	pc.oscDetuneCvAmount = 1.f;
	pc.filterCutoffCvAmount = 1.f;
	pc.resonatorTuneCvAmount = 1.f;
	pc.echoDensityCvAmount = 1.f;
	pc.ambienceSpacetimeCvAmount = 1.f;


	alg->dtc->patchState.c5 =523.25f;
    
    alg->dtc->patchState.pitchZero =0.f;
    
    alg->dtc->patchState.speedZero = 0.f;
	alg->dtc->patchState.clockSource = ClockSource::CLOCK_SOURCE_INTERNAL;

	// Initialize undo/redo history
	alg->dtc->undoCount  = 0;
    alg->dtc->undoCursor = -1;
    
	return alg;
}

void step( _NT_algorithm* self, float* busFrames, int numFramesBy4 )
{
	_OneiroiAlgorithm* pThis = (_OneiroiAlgorithm*)self;
	_OneiroiAlgorithm_DTC* dtc = pThis->dtc;

	// Apply deferred looper length parameter (set by wavLoadCallback on SD thread)
	if (dtc->wavPendingSet) {
		dtc->wavPendingSet = false;
		NT_setParameterFromAudio(NT_algorithmIndex(self),
		                         kParamlooperLength + NT_parameterOffset(),
		                         dtc->wavPendingLen);
	}

	int numFrames = numFramesBy4 * 4;
	float* outL = busFrames + ( pThis->v[kParamLeftOutput] - 1 ) * numFrames;
	float* outR = busFrames + ( pThis->v[kParamRightOutput] - 1 ) * numFrames;
	bool replaceL = pThis->v[kParamRightOutputMode];
	bool replaceR = pThis->v[kParamLeftOutputMode];

	NTSampleBuffer* myBuffer = static_cast<NTSampleBuffer*>(dtc->buffer); 
	myBuffer->setSize(numFrames);
    myBuffer->clear();
	FloatArray chLArray = myBuffer->getSamples(LEFT_CHANNEL); 
	FloatArray chRArray = myBuffer->getSamples(RIGHT_CHANNEL); 
	
	if (pThis->v[kParamLeftInput] > 0 && pThis->v[kParamRightInput] > 0){
		float* inL = busFrames + ( pThis->v[kParamLeftInput] - 1 ) * numFrames;
	    float* inR = busFrames + ( pThis->v[kParamRightInput] - 1 ) * numFrames;
		for ( int i=0; i<numFrames; ++i ){
		chRArray[i] = inR[i];
		chLArray[i] = inL[i];
		}
	} else if (pThis->v[kParamLeftInput] > 0){
		float* inL = busFrames + ( pThis->v[kParamLeftInput] - 1 ) * numFrames;
		for ( int i=0; i<numFrames; ++i ){
		chRArray[i] = inL[i];
		chLArray[i] = inL[i];
		}
	}
    if(pThis->v[kParamClockInput] > 0){
		static const float threshold = 0.5f;
		float* clockIn = busFrames + ( pThis->v[kParamClockInput] - 1) * numFrames; // Clock input is the 3rd input (index 2)
		float currentClockValue = clockIn[0]; // Use first sample for pulse detection, or use a more robust method if needed
		bool clockPulse = (dtc->prevClockValue < threshold) && (currentClockValue >= threshold);
		dtc->patchState.syncIn = clockPulse;
		dtc->prevClockValue = currentClockValue;
	}

	if(pThis->v[kParamPitchInput] > 0){
		float* PitchIn = busFrames + ( pThis->v[kParamPitchInput] - 1) * numFrames;
		pThis->dtc->pitchInput = PitchIn[0];
	} else pThis->dtc->pitchInput = 0.0f;

	dtc->patchCtrls.oscPitch = 261.63f * semi2Ratio(pThis->dtc->semi) * semi2Ratio(pThis->dtc->fine/100.f) *semi2Ratio(pThis->dtc->v8c*12.f) * semi2Ratio(pThis->dtc->pitchInput*12.f); 
	
    dtc->Oneiroi_->Process(*myBuffer);
	if ( !replaceR )
	{ // are these loops really faster than putting the if inside?
		for ( int i=0; i<numFrames; ++i ){
			outR[i] += chRArray[i];
		}
	}
	else
	{
		for ( int i=0; i<numFrames; ++i ){
			outR[i] = chRArray[i];
		}
	}

	if ( !replaceL )
	{ 
		for ( int i=0; i<numFrames; ++i ){
			outL[i] += chLArray[i];
		}
	}
	else
	{
		for ( int i=0; i<numFrames; ++i ){
			outL[i] =chLArray[i];
		}
	}
}
// do this in custom ui
void parameterChanged( _NT_algorithm* self, int p )
{
	_OneiroiAlgorithm* pThis = (_OneiroiAlgorithm*)self;
	
	
	int32_t algoIndex = NT_algorithmIndex(self);
    _OneiroiAlgorithm_DTC* dtc = pThis->dtc;

    // --- Custom Action Handling ---
    if (p == kParamActionRandomize) {
        if (pThis->v[p] > 0) {
            // Truncate any redo states above the cursor (branching history model)
            if (dtc->undoCursor >= 0 && dtc->undoCursor < dtc->undoCount - 1)
                dtc->undoCount = dtc->undoCursor + 1;

            // On the very first Randomize there is no snapshot yet: save the
            // initial (pre-randomize) state so it is reachable by Undo.
            if (dtc->undoCursor < 0) {
                int preIdx = pushSnapshot(pThis);
                dtc->undoCursor = preIdx;
            }
            // If cursor is already at the top, the invariant guarantees stack[cursor]
            // already holds the current state — no extra push needed.

            // Evict oldest entry if ring buffer is full, then claim the write slot.
            if (dtc->undoCount == UNDO_STACK_SIZE) {
                memmove(dtc->undoStack[0], dtc->undoStack[1],
                        (UNDO_STACK_SIZE - 1) * sizeof(dtc->undoStack[0]));
                dtc->undoCount--;
                if (dtc->undoCursor > 0) dtc->undoCursor--;
            }
            int postIdx = dtc->undoCount;

            // Generate random values, write them into the post-randomize snapshot
            // and fire the audio-side updates in a single pass.
            // We cannot call pushSnapshot() after NT_setParameterFromAudio() because
            // those updates are deferred — pThis->v[] won't reflect them yet, so
            // reading it afterwards would capture stale (pre-randomize) values.
            int scope = pThis->v[kParamRandScope];
            for (int i = 0; i < NUM_UNDOABLE_PARAMS; ++i) {
                int pidx = PARAM_RND_START + i;
                int16_t rv;
                if (shouldRandomize(pidx, scope)) {
                    rv = ntGetRandomValue(parameters[pidx]);
                    NT_setParameterFromAudio(algoIndex, pidx + NT_parameterOffset(), rv);
                } else {
                    rv = pThis->v[pidx]; // preserve operational params as-is
                }
                dtc->undoStack[postIdx][i] = rv;
            }
            dtc->undoCount++;
            dtc->undoCursor = postIdx;

            NT_setParameterFromAudio(algoIndex, p + NT_parameterOffset(), 0);
        }
        return;
    }
    else if (p == kParamActionUndo) {
        if (pThis->v[p] > 0) {
            if (dtc->undoCursor > 0) {
                dtc->undoCursor--;
                applySnapshot(pThis, dtc->undoCursor);
            }
            NT_setParameterFromAudio(algoIndex, p + NT_parameterOffset(), 0);
        }
        return;
    }
    else if (p == kParamActionRedo) {
        if (pThis->v[p] > 0) {
            if (dtc->undoCursor >= 0 && dtc->undoCursor < dtc->undoCount - 1) {
                dtc->undoCursor++;
                applySnapshot(pThis, dtc->undoCursor);
            }
            NT_setParameterFromAudio(algoIndex, p + NT_parameterOffset(), 0);
        }
        return;
    }
	
	auto& patchCtrls = pThis->dtc->patchCtrls;
	auto& patchState = pThis->dtc->patchState;
	
	switch (p)
	{
        // --- Handle new parameters ---
        case kParamInputLevel:
            patchCtrls.inputVol = pThis->v[p] / 10000.f; // we need to bring this back to -1.0f to 1.0f
            break;
        case kParamOutputLevel:
            patchState.outLevel = pThis->v[p] / 75.f; // Aiming for -5/+5V
            break;
    case kParamOscSemi :
		pThis->dtc->semi = pThis->v[p];
		break;
	case kParamOscFine: 
	    pThis->dtc->fine = pThis->v[p];
		break;
    case kParamOscV8c: 
		pThis->dtc->v8c = pThis->v[p]/1000.f;
		break;
	case kParamOscPitchModAmount :
	    patchCtrls.oscPitchModAmount = pThis->v[p];
		break;
	case kParamOscUnison :
	    patchCtrls.oscUnison = pThis->v[p]/10000.f;
		if (patchCtrls.oscUnison >= -0.0003f && patchCtrls.oscUnison <= 0.0003f)
        {
            patchCtrls.oscUnison = 0.f;
            patchState.oscUnisonCenterFlag = true;
        }
        else
        {
            patchState.oscUnisonCenterFlag = false;
        }
		break;
	case kParamOscDetuneModAmount:
	    patchCtrls.oscDetuneModAmount = pThis->v[p];
		break;
	case kParamOscDetune : 
		patchCtrls.oscDetune = pThis->v[p]/1000.f;
		break;
	case kParamSSOscVol : 
		patchCtrls.osc2Vol = MapExpo(pThis->v[p]/1000.f);
		break;
	case kParamSinOscVol : 
		patchCtrls.osc1Vol = MapExpo(pThis->v[p]/1000.f);
		break;
	
    case kParamfilterVol : 
	    patchCtrls.filterVol = MapExpo(pThis->v[p]/1000.f);
		break;
	    	   
    case kParamfilterMode:
		patchCtrls.filterMode = pThis->v[p]/4.f+.01f;
		break;
    case kParamfilterCutoff:
		patchCtrls.filterCutoff = pThis->v[p]/22000.f;
		break;
    case kParamfilterCutoffModAmount:
		patchCtrls.filterCutoffModAmount = pThis->v[p];
		break;
    case kParamfilterResonance:
		patchCtrls.filterResonance = pThis->v[p]/1000.f;
		break;
    case kParamfilterResonanceModAmount:
		patchCtrls.filterResonanceModAmount = pThis->v[p];
		break;
    case kParamfilterPosition:
		patchCtrls.filterPosition = pThis->v[p];
		break;

	case kParamSSWT:
		patchCtrls.oscUseWavetable = pThis->v[p]/1.f;
		break;


    case kParamlooperVol:
		patchCtrls.looperVol = MapExpo(pThis->v[p]/1000.f);
		break;

    case kParamlooperSos:
		patchCtrls.looperSos = pThis->v[p]/1000.f;  // Convert 0-1000 range to 0-1 float
		break;

    case kParamlooperFilter:
		patchCtrls.looperFilter = pThis->v[p]/1000.f;
		break;

    case kParamlooperSpeed:
		patchCtrls.looperSpeed = pThis->v[p]/1000.f;
		break;

    case kParamlooperSpeedModAmount:
		patchCtrls.looperSpeedModAmount = pThis->v[p];
		break;

    case kParamlooperStart:
		patchCtrls.looperStart = pThis->v[p]/1000.f;  // Convert 0-1000 to 0-1 range
		break;

    case kParamlooperStartModAmount:
		patchCtrls.looperStartModAmount = pThis->v[p];
		break;

    case kParamlooperLength:
		patchCtrls.looperLength = pThis->v[p]/1000.f;  // Convert 0-1000 to 0-1 range
		break;

    case kParamlooperLengthModAmount:
		patchCtrls.looperLengthModAmount = pThis->v[p];
		break;

    case kParamlooperRecording:
		patchCtrls.looperRecording = pThis->v[p] > 0.f ? 1.f : 0.f;  // Convert enum to binary
		break;

    case kParamlooperResampling:
		patchCtrls.looperResampling = pThis->v[p] > 0.f ? 1.f : 0.f;  // Convert enum to binary
		break;

	case kParamlooperClear:
		patchState.clearLooperFlag = pThis->v[p] > 0.f ? 1.f : 0.f;  // Convert enum to binary
		break;

    case kParamLooperFolder:
        dtc->looperFolder = pThis->v[p];
        break;

    case kParamLooperFile: {
        dtc->looperFile = pThis->v[p];
        int fileVal = pThis->v[p];
        if (fileVal > 0 && !dtc->wavLoading && NT_isSdCardMounted()) {
            _NT_wavInfo info;
            NT_getSampleFileInfo((uint32_t)dtc->looperFolder, (uint32_t)(fileVal - 1), info);
            if (info.numFrames > 0) {
                uint32_t frames = info.numFrames;
                if (frames > (uint32_t)kLooperChannelBufferLength)
                    frames = (uint32_t)kLooperChannelBufferLength;
                FloatArray* buf = dtc->Oneiroi_->GetLooperFloatArray();
                dtc->wavReq.folder      = (uint32_t)dtc->looperFolder;
                dtc->wavReq.sample      = (uint32_t)(fileVal - 1);
                dtc->wavReq.dst         = buf->getData();
                dtc->wavReq.numFrames   = frames;
                dtc->wavReq.startOffset = 0;
                dtc->wavReq.channels    = kNT_WavMono;
                dtc->wavReq.bits        = kNT_WavBits32;
                dtc->wavReq.progress    = kNT_WavProgress;
                dtc->wavReq.callback    = wavLoadCallback;
                dtc->wavReq.callbackData = dtc;
                dtc->wavFileFrames = frames;
                if (NT_readSampleFrames(dtc->wavReq))
                    dtc->wavLoading = true;
            }
        }
        break;
    }
		patchCtrls.resonatorVol = MapExpo(pThis->v[p]/1000.f);
		break;

    case kParamresonatorTune:
		patchCtrls.resonatorTune = pThis->v[p]/1200.f;
		break;

    case kParamresonatorFeedback:
		patchCtrls.resonatorFeedback = pThis->v[p]/1000.f;
		break;

    case kParamresonatorDissonance:
		patchCtrls.resonatorDissonance = pThis->v[p]/1000.f;
		break;

    case kParamechoVol:
		patchCtrls.echoVol = MapExpo(pThis->v[p]/1000.f);
		break;

    case kParamechoDensity:
		patchCtrls.echoDensity = pThis->v[p]/1000.f;
		break;

    case kParamechoRepeats:
		patchCtrls.echoRepeats = pThis->v[p]/1000.f;
		break;

    case kParamechoFilter:
		patchCtrls.echoFilter = pThis->v[p]/1000.f;
		break;

    case kParamambienceVol:
		patchCtrls.ambienceVol = MapExpo(pThis->v[p]/1000.f);
		break;

    case kParamambienceDecay:
		patchCtrls.ambienceDecay = pThis->v[p]/1000.f;
		break;

    case kParamambienceSpacetime:
		patchCtrls.ambienceSpacetime = pThis->v[p]/1000.f;
		break;

    case kParamambienceAutoPan:
		patchCtrls.ambienceAutoPan = pThis->v[p] > 0.f ? 1.f : 0.f;  // Convert enum to binary
		break;

    case kParammodType:
		patchCtrls.modType = pThis->v[p]/1000.f;
		break;

    case kParammodSpeed:
		patchCtrls.modSpeed = pThis->v[p]/1000.f;
		break;

    case kParammodLevel:
		patchCtrls.modLevel = pThis->v[p]/1000.f;
		break;

	default:
		break;
	}	
}

bool	draw( _NT_algorithm* self )
{
	_OneiroiAlgorithm* pThis = (_OneiroiAlgorithm*)self;
	
	char debugc[10];  	
	NT_floatToString(debugc, pThis->dtc->patchState.debugvalue);	
	NT_drawText( 10, 50, "Debug:" );
	
	NT_drawText( 60, 50, debugc );
    NT_floatToString(debugc, pThis->dtc->patchState.debugvalue2);	
	NT_drawText( 100, 50, debugc );
	NT_floatToString(debugc, pThis->dtc->patchState.debugvalue3);	
	NT_drawText( 140, 50, debugc );
    NT_floatToString(debugc, pThis->dtc->patchState.debugvalue4);	
	NT_drawText( 180, 50, debugc );

	NT_floatToString(debugc, ((float)_allocatedMemory/_allocatableMemorySize));	
	NT_drawText( 10, 35, "DRAM:" );
	NT_drawText( 60, 35, debugc );

	NT_floatToString(debugc, ((float)_allocatedDTCMemory/_allocatableDTCMemorySize));	
	NT_drawText( 100, 35, "DTC:" );
	NT_drawText( 140, 35, debugc );
		
	return false;
}

// ---------------------------------------------------------------------------
// parameterString — display names for kNT_unitHasStrings parameters.
// ---------------------------------------------------------------------------
static int parameterString(_NT_algorithm* self, int p, int v, char* buff) {
    _OneiroiAlgorithm* pThis = (_OneiroiAlgorithm*)self;
    int len = 0;
    switch (p) {
    case kParamLooperFolder: {
        _NT_wavFolderInfo info;
        NT_getSampleFolderInfo((uint32_t)v, info);
        if (info.name) {
            strncpy(buff, info.name, kNT_parameterStringSize - 1);
            buff[kNT_parameterStringSize - 1] = '\0';
            len = strlen(buff);
        }
        break;
    }
    case kParamLooperFile: {
        if (v == 0) {
            strncpy(buff, "None", kNT_parameterStringSize - 1);
            buff[kNT_parameterStringSize - 1] = '\0';
            len = strlen(buff);
        } else {
            _NT_wavInfo info;
            NT_getSampleFileInfo((uint32_t)pThis->dtc->looperFolder, (uint32_t)(v - 1), info);
            if (info.name) {
                strncpy(buff, info.name, kNT_parameterStringSize - 1);
                buff[kNT_parameterStringSize - 1] = '\0';
                len = strlen(buff);
            }
        }
        break;
    }
    default:
        break;
    }
    return len;
}

static const _NT_factory factory = 
{
	.guid = NT_MULTICHAR( 'B', 'o', 'O', 'I' ),
	.name = "Oneiroi",
	.description = "Oneiroi from Befaco",
	.numSpecifications = 0,
	.specifications = NULL,
	.calculateRequirements = calculateRequirements,
	.construct = construct,
	.parameterChanged = parameterChanged,
	.step = step,
	.draw = draw,
	.midiMessage = NULL,
	.parameterString = parameterString,
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