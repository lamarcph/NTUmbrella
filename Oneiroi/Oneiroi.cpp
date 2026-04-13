#include <math.h>
#include <new>
#include <distingnt/api.h>
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
    int     undoHead = 0;       // Index where the NEXT state will be written
    int     undoSize = 0;       // Number of valid undo states available
    int     undoCurrentIndex = 0; // The index of the currently loaded stat
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

	{ .name = "Randomize", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsTrigger },
    { .name = "Undo",      .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsTrigger },
    { .name = "Redo",      .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsTrigger },
};  


static const uint8_t page1[] = { kParamOscSemi,	kParamOscFine,kParamOscV8c, kParamOscDetune, kParamOscPitchModAmount, kParamOscUnison, kParamOscDetuneModAmount, kParamSSOscVol, kParamSinOscVol, kParamSSWT};
static const uint8_t page2[] = {kParamfilterMode, kParamfilterCutoff, kParamfilterCutoffModAmount, kParamfilterResonance, kParamfilterResonanceModAmount, kParamfilterPosition, kParamfilterVol};
static const uint8_t page3[] = { kParamlooperVol,kParamlooperSos, kParamlooperFilter, kParamlooperSpeed, kParamlooperSpeedModAmount, kParamlooperStart, kParamlooperStartModAmount, kParamlooperLength, kParamlooperLengthModAmount, kParamlooperRecording, kParamlooperResampling, kParamlooperClear};
static const uint8_t page4[] = { kParamresonatorVol, kParamresonatorTune, kParamresonatorFeedback, kParamresonatorDissonance};
static const uint8_t page5[] = { kParamechoVol, kParamechoDensity, kParamechoRepeats, kParamechoFilter };
static const uint8_t page6[] = { kParamambienceVol, kParamambienceDecay, kParamambienceSpacetime, kParamambienceAutoPan };
static const uint8_t page7[] = { kParammodType, kParammodSpeed, kParammodLevel};
static const uint8_t page8[] = { kParamActionRandomize, kParamActionUndo, kParamActionRedo};
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


// Helper: Get random value based on parameter definition
int16_t getRandomValue(const _NT_parameter& p) {
    if (p.unit == kNT_unitEnum) {
        // For enums, pick a random index
        return p.min + (rand() % (p.max - p.min + 1));
    }
    // For others, pick a random value in range
    // You might want to constrain this to avoid extreme values
    return p.min + (rand() % (p.max - p.min + 1));
}

// Helper: Save current state to the undo stack
void saveCurrentState( _OneiroiAlgorithm* self ) {
    _OneiroiAlgorithm_DTC* dtc = self->dtc;
    
    // If we are in the middle of the stack (Undo was used), invalidate the "future" (Redo)
    if (dtc->undoCurrentIndex != dtc->undoHead) {
        dtc->undoHead = dtc->undoCurrentIndex;
        dtc->undoSize = dtc->undoCurrentIndex; // Simplified stack logic
    }

    // Save current values
    int writeIdx = dtc->undoHead;
    for (int i = 0; i < NUM_UNDOABLE_PARAMS; ++i) {
        int paramIdx = PARAM_RND_START + i;
        dtc->undoStack[writeIdx][i] = self->v[paramIdx];
    }

    // Advance head
    dtc->undoHead = (dtc->undoHead + 1) % UNDO_STACK_SIZE;
    // Update current index to point to the new empty slot (or the one we just wrote, depending on logic)
    // Here: undoCurrentIndex points to the state we are *currently looking at*. 
    // Actually, it's easier if undoHead points to the *next empty* slot.
    // Let's say undoCurrentIndex is the index of the *last saved state*.
    
    dtc->undoCurrentIndex = writeIdx;
    
    if (dtc->undoSize < UNDO_STACK_SIZE) dtc->undoSize++;
}

// Helper: Load state from stack and update UI
void applyState( _OneiroiAlgorithm* self, int stackIndex ) {
    _OneiroiAlgorithm_DTC* dtc = self->dtc;
    int32_t algoIndex = NT_algorithmIndex(self);

    for (int i = 0; i < NUM_UNDOABLE_PARAMS; ++i) {
        int paramIdx = PARAM_RND_START + i;
        int16_t storedVal = dtc->undoStack[stackIndex][i];
        
        // Only update if changed to minimize message traffic
        if (self->v[paramIdx] != storedVal) {
             NT_setParameterFromUi( algoIndex, paramIdx + NT_parameterOffset(), storedVal );
        }
    }
    dtc->undoCurrentIndex = stackIndex;
}

void performRandomize( _OneiroiAlgorithm* self ) {
    // 1. Save current state first
    //saveCurrentState(self);

    // 2. Randomize
    int32_t algoIndex = NT_algorithmIndex(self);
    for (int p = PARAM_RND_START; p <= PARAM_RND_END; ++p) {
        // Skip if you want to protect certain params (like Vol/Pitch)
        // if (p == kParamOscSemi) continue; 
        
        const _NT_parameter& paramDef = self->parameters[p];
        int16_t newVal = getRandomValue(paramDef);
        NT_setParameterFromUi( algoIndex, p + NT_parameterOffset(), newVal );
    }
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

	// Initialize undo stack variables
	alg->dtc->undoHead = 0;
    alg->dtc->undoSize = 0;
    alg->dtc->undoCurrentIndex = -1; // Indicates no history yet
    
	return alg;
}

void step( _NT_algorithm* self, float* busFrames, int numFramesBy4 )
{
	_OneiroiAlgorithm* pThis = (_OneiroiAlgorithm*)self;
	_OneiroiAlgorithm_DTC* dtc = pThis->dtc;
	
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
        if (pThis->v[p] > 0) { // Triggered (Value 1)
            // If stack is empty, save the *initial* state of the module first
            if (dtc->undoCurrentIndex == -1) {
                // Initialize head/current to 0 and save
                dtc->undoHead = 0;
                dtc->undoCurrentIndex = 0;
                // Manually save current v to slot 0
                for (int i = 0; i < NUM_UNDOABLE_PARAMS; ++i) {
                     dtc->undoStack[0][i] = pThis->v[PARAM_RND_START + i];
                }
                dtc->undoHead = 1;
                dtc->undoSize = 1;
            }

            performRandomize(pThis);

            // Reset button to 0 (Idle)
            NT_setParameterFromUi( algoIndex,p + NT_parameterOffset(), 0 );
        }
        return;
    }
    else if (p == kParamActionUndo) {
        if (pThis->v[p] > 0) {
            // Check if we can Undo
            if (dtc->undoCurrentIndex > 0) {
                // Determine previous index (simple linear or circular logic)
                // Using circular logic from previous example:
                int prevIndex = (dtc->undoCurrentIndex - 1 + UNDO_STACK_SIZE) % UNDO_STACK_SIZE;
                
                // Safety check against wrapping around to invalid data if not full
                if (dtc->undoSize < UNDO_STACK_SIZE && prevIndex > dtc->undoCurrentIndex) {
                     // logic error or empty, do nothing
                } else {
                     applyState(pThis, prevIndex);
                }
            }
            NT_setParameterFromUi( algoIndex,p + NT_parameterOffset(), 0 );
        }
        return;
    }
    else if (p == kParamActionRedo) {
        if (pThis->v[p] > 0) {
            // Check if we can Redo (next index exists and is valid)
            int nextIndex = (dtc->undoCurrentIndex + 1) % UNDO_STACK_SIZE;
            
            // We can only redo if nextIndex is logically "ahead" and valid
            // In a simple stack, redo is possible if undoCurrentIndex < (head - 1)
            // This circular buffer logic needs to match exactly how you increment head.
            // For simplicity, if we are not at the head-1, we can go forward.
            
            if (nextIndex != dtc->undoHead) { 
                applyState(pThis, nextIndex);
            }
            NT_setParameterFromUi( algoIndex,p + NT_parameterOffset(), 0 );
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

    case kParamresonatorVol:
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