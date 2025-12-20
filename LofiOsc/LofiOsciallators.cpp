#include <math.h>
#include <new>
#include <distingnt/api.h>
#include <array>
#include <LofiMorphOscillator.h>
#include <CheapMaths.h> // Add this include
#include <Q15Effects.h>

#define NUM_OSC 9
#define BUFFER_LENGHT 128

struct _lofiOscAlgorithm_DTC
{
    uint32_t	phase;
    uint32_t	inc;
    OscillatorFixedPoint osc[NUM_OSC]; // Array of 8 oscillators
    float semi = 0.f, fine = 0.f, v8c = 0.f, freq = 0.f;
    int waveform = 0;
    float morph = 0.f;
    float detune = 0.f;
    int detuneType = 0;
    float gain = 1.f;
    float fmDepth = 0.f;      // Add FM depth param
    float morphModDepth = 1.f; // Add Morph Mod depth param
    int16_t softClipping = 0;
    bool resetPhases = false;
    
    std::array<int32_t,BUFFER_LENGHT> sumBuffer;
    std::array<int16_t,BUFFER_LENGHT> genBuffer;
    std::array<int16_t,BUFFER_LENGHT> linFmBuffer;
    std::array<int16_t,BUFFER_LENGHT> inBufferV8o;
    std::array<int16_t,BUFFER_LENGHT> morphBuffer;
    std::array<int16_t,BUFFER_LENGHT> harmBuffer;
    std::array<float, NUM_OSC> ampSmoothed;
    
    float currentHarmonicMod = 0;
    float previousHarmonicMod = 0;

    float debugvalue, debugvalue2, debugvalue3, debugvalue4;
    // ------------------------
};

struct PartialParams {
    float ratio;
    float amplitude;
    float decayTime;
};

constexpr int NUM_MODELS = 7;
constexpr int NUM_PARTIALS = 9;

const char* modelNames[NUM_MODELS] = {
    "tr808", "hammond", "piano", "bell", "marimba", "bass drum"
};

static char const * const enumStringsHoldMode[] = {
    "Gate-Driven (AHR)",
    "Timed (Fixed Length)"
    "Env Disabled"
};

PartialParams modelParams[NUM_MODELS][NUM_PARTIALS] = {
    // tr808
    {
        {1.0f, 1.0f, 1.0f},
        {1.1372f, 0.8f, 0.83f},
        {1.481f, 0.7f, 0.72f},
        {1.53f, 0.7f, 0.61f},
        {2.165f, 0.6f, 0.50f},
        {2.628f, 0.6f, 0.44f},
        {3.897f, 0.5f, 0.39f},
        {4.124f, 0.5f, 0.33f},
        {5.320f, 0.4f, 0.28f}
    },
    // hammond
    {
        {0.5f, 1.0f, 1.0f},
        {1.0f, 0.3f, 0.95f},
        {1.5f, 0.3f, 0.90f},
        {2.0f, 0.5f, 0.85f},
        {3.0f, 0.5f, 0.80f},
        {4.0f, 0.6f, 0.75f},
        {5.0f, 0.3f, 0.70f},
        {6.0f, 2.0f, 0.65f},
        {8.0f, 5.0f, 0.60f}
    },
    // piano
    {
        {1.0f, 1.0f, 1.0f},
        {2.01f, 0.7f, 0.75f},
        {3.02f, 0.5f, 0.60f},
        {4.03f, 0.35f, 0.50f},
        {5.04f, 0.25f, 0.40f},
        {6.05f, 0.18f, 0.35f},
        {7.06f, 0.13f, 0.30f},
        {8.07f, 0.09f, 0.25f},
        {9.08f, 0.06f, 0.20f}
    },
    // bell
    {
        {1.0f, 1.0f, 1.0f},
        {2.0f, 0.8f, 0.88f},
        {2.7f, 0.7f, 0.75f},
        {3.8f, 0.6f, 0.63f},
        {4.8f, 0.5f, 0.50f},
        {5.9f, 0.4f, 0.38f},
        {6.8f, 0.3f, 0.30f},
        {8.9f, 0.2f, 0.25f},
        {9.7f, 0.15f, 0.20f}
    },
    // marimba
    {
        {1.0f, 1.0f, 1.0f},
        {2.0f, 0.6f, 0.80f},
        {3.0f, 0.4f, 0.60f},
        {4.05f, 0.3f, 0.50f},
        {5.1f, 0.22f, 0.40f},
        {6.15f, 0.16f, 0.30f},
        {7.2f, 0.12f, 0.20f},
        {8.25f, 0.09f, 0.15f},
        {9.3f, 0.07f, 0.10f}
    },
    // bass drum
    {
        {1.00f, 1.0f, 1.0f},
        {1.55f, 0.5f, 0.57f},
        {2.15f, 0.35f, 0.36f},
        {2.85f, 0.25f, 0.26f},
        {3.60f, 0.18f, 0.19f},
        {4.40f, 0.13f, 0.13f},
        {5.25f, 0.09f, 0.09f},
        {6.15f, 0.06f, 0.06f},
        {7.10f, 0.04f, 0.04f}
    },
    // Harmonics series
    {
        {1.0f, 1.0f, 1.0f},
        {2.0f, 0.8f, 0.9f},
        {3.0f, 0.6f, 0.8f},
        {4.0f, 0.5f, 0.7f},
        {5.0f, 0.4f, 0.6f},
        {6.0f, 0.3f, 0.5f},
        {7.0f, 0.2f, 0.4f},
        {8.0f, 0.15f, 0.3f},
        {9.0f, 0.1f, 0.2f}
    }
};




struct _lofiOscAlgorithm : public _NT_algorithm
{
	_lofiOscAlgorithm( _lofiOscAlgorithm_DTC* dtc_ ) : dtc( dtc_ ) {}
	~_lofiOscAlgorithm() {}
	
	_lofiOscAlgorithm_DTC*	dtc;
};

enum
{
    kParamOutput,
    kParamOutputMode,
    kParamLinFMInput,
    kParamV8Input,
    kParamMorphInput,
    kParamHarmonicsInput,
    kParamWaveform,
    kParamOscSemi,
    kParamOscFine,
    kParamOscV8c,
    kParamOscMorph,
    kParamDetune,
    kParamDetuneType,
    kParamGain,
    kParamFmDepth,        
    kParamMorphModDepth,  
    kParamHarmonics,      
};

static char const * const enumStringsWaveform[] = {
	"Sine",
    "Square",
	"Triangle",
    "Sawtooth",
    "Morph"
};

static char const * const enumStringsDetuneType[] = {
    "Tri bell",
    "Tr-808",
    "Hammond",
    "Piano",
    "Bell",
    "Marimba",
    "Bass drum",
    "Harmonics serie"
};


static const _NT_parameter	parameters[] = {
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Output", 1, 13 )
    NT_PARAMETER_AUDIO_INPUT( "Lin/TZFM FM input", 0, 0 )
    NT_PARAMETER_AUDIO_INPUT( "V/8 input", 0, 0 )
    NT_PARAMETER_AUDIO_INPUT( "Morph input", 0, 0 )
    NT_PARAMETER_AUDIO_INPUT( "Harmonics input", 0, 0 )
    { .name = "Waveform", .min = 0, .max = 4, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsWaveform },
    { .name = "Semi", .min = -48, .max = 48, .def = 0, .unit = kNT_unitSemitones, .scaling = 0, .enumStrings = NULL },
    { .name = "Fine", .min = -50, .max = 50, .def = 0, .unit = kNT_unitCents, .scaling = 0, .enumStrings = NULL },
    { .name = "Volt/Octave", .min = -5000, .max = 5000, .def = 0, .unit = kNT_unitVolts, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Morph", .min = 0, .max = 1000, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "Detune", .min = 0, .max = 20000, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "Tuning Model", .min = 0, .max = 7, .def = 0, .unit = kNT_unitEnum, .scaling = kNT_scaling1000, .enumStrings = enumStringsDetuneType },
    { .name = "Gain", .min = 0, .max = 20000, .def = 5000, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },
    { .name = "FM depth", .min = 0, .max = 22000, .def = 1000, .unit = kNT_unitHz, .scaling = 0, .enumStrings = NULL },
    { .name = "Morph mod depth", .min = 0, .max = 1000, .def = 1000, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL },
    { .name = "Harmonics", .min = 0, .max = 1000, .def = 1000, .unit = kNT_unitNone, .scaling = kNT_scaling1000, .enumStrings = NULL }

};

// --- MODIFIED: Page Definitions (Curve Mix Added) ---
static const uint8_t page1[] = { kParamWaveform, kParamOscSemi, kParamOscFine, kParamOscV8c, kParamOscMorph, kParamDetune, kParamDetuneType, kParamGain, kParamFmDepth, kParamMorphModDepth, kParamHarmonics};

static const uint8_t page2[] = { kParamOutput, kParamOutputMode, kParamLinFMInput, kParamV8Input, kParamMorphInput, kParamHarmonicsInput }; 

static const _NT_parameterPage pages[] = {
    { .name = "Setup", .numParams = ARRAY_SIZE(page1), .params = page1 },
    { .name = "Routing", .numParams = ARRAY_SIZE(page2), .params = page2 }
};

static const _NT_parameterPages parameterPages = {
	.numPages = ARRAY_SIZE(pages),
	.pages = pages,
};

void calculateRequirements( _NT_algorithmRequirements& req, const int32_t* specifications )
{
	req.numParameters = ARRAY_SIZE(parameters);
	req.sram = sizeof(_lofiOscAlgorithm);
	req.dram = 0;
	req.dtc = sizeof(_lofiOscAlgorithm_DTC);
	req.itc = 0;
}

_NT_algorithm*	construct( const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req, const int32_t* specifications )
{
    _lofiOscAlgorithm_DTC* dtc = new (ptrs.dtc) _lofiOscAlgorithm_DTC();
	_lofiOscAlgorithm* alg = new (ptrs.sram) _lofiOscAlgorithm( (_lofiOscAlgorithm_DTC*)ptrs.dtc );
	alg->parameters = parameters;
	alg->parameterPages = &parameterPages;
    for (int i = 0; i < NUM_OSC; ++i) dtc->osc[i].setSampleRate(NT_globals.sampleRate);
    for (int i = 0; i< BUFFER_LENGHT; ++i){
        dtc->sumBuffer[i]=0;
    }
    dtc->osc[0].setdebugValuePointers(&dtc->debugvalue, &dtc->debugvalue2, &dtc->debugvalue3, &dtc->debugvalue4);
    
    for (int i = 0; i < NUM_OSC; ++i) {
        dtc->ampSmoothed[i] = static_cast<float>(Q15_MAX_VAL); // start at unity (Q15)
    }
	return alg;
}
float semi2Ratio(float semi) { return fast_powf(2.0f, semi / 12.0f); }


void parameterChanged(_NT_algorithm* self, int p)
{
    _lofiOscAlgorithm* pThis = (_lofiOscAlgorithm*)self;
    _lofiOscAlgorithm_DTC* dtc = pThis->dtc;

    bool freqChanged = false;
    switch (p)
    {
        case kParamOscSemi:
            dtc->semi = pThis->v[p];
            freqChanged = true;
            break;
        case kParamOscFine:
            dtc->fine = pThis->v[p];
            freqChanged = true;
            break;
        case kParamOscV8c:
            dtc->v8c = pThis->v[p] / 1000.f;
            freqChanged = true;
            break;
        case kParamWaveform:
            dtc->waveform = pThis->v[p];
            break;
        case kParamOscMorph:
            dtc->morph = pThis->v[p] / 1000.f;
            for (int i = 0; i < NUM_OSC; ++i)
                dtc->osc[i].setShapeMorph(dtc->morph);
            break;
        case kParamDetune:
            dtc->detune = pThis->v[p] / 20000.f;
            freqChanged = true;
            break;
        case kParamDetuneType:
            dtc->detuneType = pThis->v[p];
            freqChanged = true;
            break;
        case kParamGain:
            dtc->gain = pThis->v[p] / 1000.f;
            break;
        case kParamFmDepth:
            dtc->fmDepth = pThis->v[p]  * 1.f; // Scale to Hz range
            for (int i = 0; i < NUM_OSC; ++i)
                dtc->osc[i].setFmDepth(dtc->fmDepth);
            break;
        case kParamMorphModDepth:
            dtc->morphModDepth = pThis->v[p] / 1000.f;
            for (int i = 0; i < NUM_OSC; ++i)
                dtc->osc[i].setMorphModDepth(dtc->morphModDepth);
            break;
        case kParamHarmonics:
          {
            dtc->currentHarmonicMod = 1.f - pThis->v[p] / 1000.f;
            break;
          }

        default:
            break;
    }
    if (freqChanged){
        float baseFreq = 261.63f; // Middle C
        dtc->freq = baseFreq
            * semi2Ratio(dtc->semi)
            * semi2Ratio(dtc->fine / 100.f)
            * semi2Ratio(dtc->v8c * 12.f);
        for (int i = 0; i < NUM_OSC; ++i)
            if (dtc->detune>0.001f){
                dtc->resetPhases = true;
                if (dtc->detuneType==0){
                float detune = uint16_to_float(get_triangular_dist(), -dtc->detune, dtc->detune);
                dtc->osc[i].setFrequency(dtc->freq + dtc->freq *detune);
                } else {
                    int modelnb = dtc->detuneType -1;
                    dtc->osc[i].setFrequency(dtc->freq * ( 1+((modelParams[modelnb][i].ratio-1.f)*2.f* dtc->detune ) ));
                }                
            } else {
            dtc->osc[i].setFrequency(dtc->freq);
            if (dtc->resetPhases){
            dtc->osc[i].hardSync(); // only when getting out of detune
            dtc->resetPhases = false;
            }
        }
    }
}

void step( _NT_algorithm* self, float* busFrames, int numFramesBy4 )
{
    _lofiOscAlgorithm* pThis = (_lofiOscAlgorithm*)self;
    _lofiOscAlgorithm_DTC* dtc = pThis->dtc;
    
    int numFrames = numFramesBy4 * 4;
    float* out = busFrames + ( pThis->v[kParamOutput] - 1 ) * numFrames;
    bool replace = pThis->v[kParamOutputMode];
    int waveform = pThis->v[kParamWaveform];

    float* inLinFmBuffer = busFrames + ( pThis->v[kParamLinFMInput] - 1 ) * numFrames;
	float* inBufferV8o = busFrames + ( pThis->v[kParamV8Input] - 1 ) * numFrames;
    float* inMorphBuffer = busFrames + ( pThis->v[kParamMorphInput] - 1 ) * numFrames;
    float* inHarmonicsBuffer = busFrames + ( pThis->v[kParamHarmonicsInput] - 1 ) * numFrames;
	bool processV8 = pThis->v[kParamV8Input] !=0;
    bool processFm = pThis->v[kParamLinFMInput] !=0;
    bool processMorph = pThis->v[kParamMorphInput] !=0;
    bool processHarmonics = pThis->v[kParamHarmonicsInput] !=0;
        
    if (processFm){
      for (int i = 0; i< numFrames; ++i){
        dtc->linFmBuffer[i] =  static_cast<int16_t>( inLinFmBuffer[i] / 5.f * Q15_MAX_VAL);
      }
    }

    if (processMorph){
      for (int i = 0; i< numFrames; ++i){
        dtc->morphBuffer[i] =  static_cast<int16_t>( inMorphBuffer[i] / 5.f * Q15_MAX_VAL);
      }
    }

    for (int i = 0; i< numFrames; ++i){
        dtc->sumBuffer[i]=0;
        dtc->genBuffer[i]=0;
    }

    dtc->debugvalue = dtc->currentHarmonicMod;
    for (int oscIdx = 0; oscIdx < NUM_OSC; ++oscIdx)
    {                
        if (processV8){dtc->osc[oscIdx].prepareVOctBlock(inBufferV8o, numFrames, processFm);}
        if (processFm) dtc->osc[oscIdx].prepareFmBlock(&(dtc->linFmBuffer[0]), numFrames);
        if (processMorph )dtc->osc[oscIdx].prepareMorphBlock(&(dtc->morphBuffer[0]), numFrames);
        switch (waveform)
        {
            case 0: dtc->osc[oscIdx].getSineWaveBlock(&(dtc->genBuffer[0]),numFrames); break;
            case 1: dtc->osc[oscIdx].getSquareWaveBlock(&(dtc->genBuffer[0]),numFrames); break;
            case 2: dtc->osc[oscIdx].getTriangleWaveBlock(&(dtc->genBuffer[0]),numFrames); break;
            case 3: dtc->osc[oscIdx].getSawWaveBlock(&(dtc->genBuffer[0]),numFrames); break;
            case 4: dtc->osc[oscIdx].getMorphedWaveBlock(&(dtc->genBuffer[0]),numFrames); break;
            default: break;
        }
        for (int i = 0; i< numFrames; ++i){
            // --- compute target amplitude (float), then smooth it to avoid clicks ---
            float harmValue = dtc->currentHarmonicMod;
            if (processHarmonics) {
                harmValue -= inHarmonicsBuffer[i] / 10.f;
                harmValue = clampf(harmValue, 0.f, 1.f);
                dtc->debugvalue2 = inHarmonicsBuffer[i];
                dtc->debugvalue3 = harmValue;
            }

            float target_a;
            if (dtc->detuneType == 0) {
                target_a = std::max(1.f, 1.f - (static_cast<float>(oscIdx) * 0.1f) + harmValue);
            } else {
                int modelIdx = dtc->detuneType - 1;
                // Smoothstep-based falloff (cheap alternative to exp)
                // decayTime behaves like a time/scale factor; larger values => slower decay.
                float decay = modelParams[modelIdx][oscIdx].decayTime;
                float t = (decay > 0.f) ? (harmValue / decay) : harmValue;
                if (t < 0.f) t = 0.f;
                else if (t > 1.f) t = 1.f;
                target_a = modelParams[modelIdx][oscIdx].amplitude
                           * fast_expf(1.f - harmValue / modelParams[modelIdx][oscIdx].decayTime);            }

            // Convert target to Q1.15 range (still as float)
            float target_q15_f = target_a * static_cast<float>(Q15_MAX_VAL);

            // Smooth the amplitude (one-pole). Alpha chosen small to remove clicks but keep responsiveness.
            // You can tweak alpha: 0.2f = fairly fast, 0.05f = slower smoothing.
            const float ampSmoothAlpha = 0.2f;
            dtc->ampSmoothed[oscIdx] += (target_q15_f - dtc->ampSmoothed[oscIdx]) * ampSmoothAlpha;

            // Convert smoothed amplitude to int16 Q15 with rounding and clamp
            int32_t amp_q15_i = static_cast<int32_t>(dtc->ampSmoothed[oscIdx] + 0.5f);
            if (amp_q15_i > Q15_MAX_VAL) amp_q15_i = Q15_MAX_VAL;
            if (amp_q15_i < 0) amp_q15_i = 0;
            int16_t amp_q15 = static_cast<int16_t>(amp_q15_i);

            // Apply amplitude to generated sample
            dtc->sumBuffer[i] += ((int32_t)dtc->genBuffer[i] * (int32_t)amp_q15) >> 15;
        }
    }    
    
    for (int i = 0; i< numFrames; ++i){
        float sample = (dtc->sumBuffer[i] / NUM_OSC / 32767.0f) * dtc->gain; // Apply gain
        if (replace)
            out[i] = sample;
        else
            out[i] += sample;
    }
}

bool	draw( _NT_algorithm* self )
{
    _lofiOscAlgorithm* pThis = (_lofiOscAlgorithm*)self;
    	
	char debugc[10];  	
	NT_floatToString(debugc, pThis->dtc->debugvalue);	
	NT_drawText( 10, 50, "Debug:" );
	
	NT_drawText( 60, 50, debugc );
    NT_floatToString(debugc, pThis->dtc->debugvalue2);	
	NT_drawText( 100, 50, debugc );
	NT_floatToString(debugc, pThis->dtc->debugvalue3);	
	NT_drawText( 140, 50, debugc );
    NT_floatToString(debugc, pThis->dtc->debugvalue4,8);	
	NT_drawText( 180, 50, debugc );
		
	return false;
}

static const _NT_factory factory = 
{
    .guid = NT_MULTICHAR( 'P', 'L', 'l', 'o' ),
    .name = "Lofi osciallators",
    .description = "9 detunable lo-fi oscillators with morphing and FM",
    .numSpecifications = 0,
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