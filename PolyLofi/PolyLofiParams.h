// =============================================================================
// PolyLofiParams.h — Canonical parameter enum, string tables, sync multipliers
// =============================================================================
// Single source of truth for all PolyLofi parameter indices and enum strings.
// Included by PolyLofi.cpp (plugin) and test_integration.cpp (tests).
// =============================================================================
#pragma once

// ---------------------------------------------------------------------------
// Parameter indices
// ---------------------------------------------------------------------------
enum
{
    kParamOutput,
    kParamOutputMode,
    kParamBaseCutoff,
    kParamResonance,
    kParamFilterEnvAmount,
    kParamFilterMode,
    kParamDrive,

    kParamOsc1Waveform,
    kParamOsc1Semitone,
    kParamOsc1Fine,
    kParamOsc1Morph,
    kParamOsc1Level,

    kParamOsc2Waveform,
    kParamOsc2Semitone,
    kParamOsc2Fine,
    kParamOsc2Morph,
    kParamOsc2Level,

    kParamOsc3Waveform,
    kParamOsc3Semitone,
    kParamOsc3Fine,
    kParamOsc3Morph,
    kParamOsc3Level,

    kParamAmpAttack,
    kParamAmpDecay,
    kParamAmpSustain,
    kParamAmpRelease,
    kParamAmpShape,
    kParamDelayTime,
    kParamDelayFeedback,
    kParamDelayMix,
    kParamFilterAttack,
    kParamFilterDecay,
    kParamFilterSustain,
    kParamFilterRelease,
    kParamFilterShape,
    kParamMidiChannel,

    // LFO1
    kParamLfoSpeed,
    kParamLfoShape,
    kParamLfoUnipolar,
    kParamLfoMorph,
    kParamLfo1SyncMode,
    kParamLfo1KeySync,

    // LFO2
    kParamLfo2Speed,
    kParamLfo2Shape,
    kParamLfo2Unipolar,
    kParamLfo2Morph,
    kParamLfo2SyncMode,
    kParamLfo2KeySync,

    // LFO3
    kParamLfo3Speed,
    kParamLfo3Shape,
    kParamLfo3Unipolar,
    kParamLfo3Morph,
    kParamLfo3SyncMode,
    kParamLfo3KeySync,

    kParamMod1Source,
    kParamMod1Dest,
    kParamMod1Amount,
    kParamMod2Source,
    kParamMod2Dest,
    kParamMod2Amount,
    kParamMod3Source,
    kParamMod3Dest,
    kParamMod3Amount,
    kParamMod4Source,
    kParamMod4Dest,
    kParamMod4Amount,
    kParamModEnvAttack,
    kParamModEnvDecay,
    kParamModEnvSustain,
    kParamModEnvRelease,
    kParamModEnvShape,
    kParamFM3to2,
    kParamFM3to1,
    kParamFM2to1,
    kParamSync3to2,
    kParamSync3to1,
    kParamSync2to1,
    kParamGlideTime,
    kParamGlideMode,
    kParamBitCrush,
    kParamSampleReduce,
    kParamRightOutput,
    kParamRightOutputMode,
    kParamOsc1Wavetable,
    kParamOsc2Wavetable,
    kParamOsc3Wavetable,
    kParamPanSpread,
    kParamDelaySyncMode,
    kParamDelayDiffusion,
    kParamClockInput,
    kParamDelayFBFilter,
    kParamDelayFBFreq,
    kParamDelayPitchTrack,
    kParamMasterVolume,
    kParamFilterModel,
    kParamKeyboardTracking,
    kParamLegato,
    kParamLfo1CutoffMod,
    kParamLfo2VibratoMod,
    kParamVelocitySens,

    // --- Preset control params (not stored in presets) ---
    kParamLoadPreset,
    kParamSavePreset,
    kParamSaveConfirm,
    kNumParams
};

// Setup params — excluded from internal preset save/load.
// These control routing, output, and MIDI configuration.
inline bool isSetupParam(int p) {
    return p == kParamOutput || p == kParamOutputMode ||
           p == kParamRightOutput || p == kParamRightOutputMode ||
           p == kParamMasterVolume || p == kParamPanSpread ||
           p == kParamMidiChannel || p == kParamClockInput;
}

// ---------------------------------------------------------------------------
// Enum string tables (used by NT parameter descriptors and optionally by tests)
// ---------------------------------------------------------------------------
static char const * const enumStringsMidiChannel[] = {
    "All", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"
};

static char const * const enumStringsFilterMode[] = {
    "LP2", "LP4", "HP2", "BP2", "NOTCH2", "HP2_LP2", "BYPASS"
};

static char const * const enumStringsFilterModel[] = {
    "SVF", "Ladder", "MS-20", "Diode"
};

static char const * const enumStringsWaveform[] = {
    "Sine",
    "Square",
    "Triangle",
    "Sawtooth",
    "Morph",
    "PolyBLEP Saw",
    "PolyBLEP Sqr",
    "Wavetable",
    "Noise"
};

static char const * const enumStringsModSource[] = {
    "Off", "LFO", "LFO2", "LFO3", "Amp Env", "Filter Env", "Mod Env", "Velocity", "Mod Wheel", "Aftertouch", "Note Random", "Key Track"
};

static char const * const enumStringsModDest[] = {
    "Cutoff", "Resonance", "Amp Attack", "Amp Decay", "Amp Release", "Filter Attack", "Filter Decay", "Filter Release", "Osc1 Morph", "Osc2 Morph", "Osc3 Morph", "All Morph", "FM 3>2", "FM 3>1", "FM 2>1", "Delay Time", "Delay Fdbk", "Delay Mix", "Pitch", "Drive", "Flt Env Amt", "Osc1 Level", "Osc2 Level", "Osc3 Level", "Osc1 Pitch", "Osc2 Pitch", "Osc3 Pitch", "LFO Speed"
};

static char const * const enumStringsLfoShape[] = {
    "Sine", "Triangle", "Square", "Saw", "Morph", "S&H"
};

static char const * const enumStringsGlideMode[] = {
    "Off", "Always", "Legato"
};

static char const * const enumStringsOnOff[] = {
    "Off", "On"
};

static char const * const enumStringsLfoSyncMode[] = {
    "Free", "4 bar", "2 bar", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/4T", "1/8T", "1/4.", "1/8."
};

static char const * const enumStringsDelayFBFilter[] = {
    "Off", "LP", "HP"
};

static char const * const enumStringsDelayPitchTrack[] = {
    "Off", "Unison", "Oct -1", "Oct +1", "Fifth"
};

// Pitch-tracked comb delay period multipliers.
// Index 0 unused (Off). Indices 1-4 map to enumStringsDelayPitchTrack.
static const float kPitchTrackMultipliers[] = {
    1.0f,           // 0: Off (unused)
    1.0f,           // 1: Unison — period = SR / f0
    0.5f,           // 2: Oct -1 — period = SR / (f0 / 2) = 2 * SR / f0
    2.0f,           // 3: Oct +1 — period = SR / (f0 * 2) = SR / (2 * f0)
    1.5f            // 4: Fifth — period = SR / (f0 * 3/2)
};

// ---------------------------------------------------------------------------
// Sync multiplier table: LFO_Hz = (BPM / 60) * kSyncMultipliers[mode]
// Index 0 is unused (Free mode). Indices 1–11 map to enumStringsLfoSyncMode.
// ---------------------------------------------------------------------------
static const float kSyncMultipliers[] = {
    0.0f,           // 0: Free (unused)
    1.0f / 16.0f,   // 1: 4 bar
    1.0f / 8.0f,    // 2: 2 bar
    1.0f / 4.0f,    // 3: 1 bar
    1.0f / 2.0f,    // 4: 1/2
    1.0f,           // 5: 1/4 (quarter note)
    2.0f,           // 6: 1/8
    4.0f,           // 7: 1/16
    3.0f / 2.0f,    // 8: 1/4T (triplet)
    3.0f,           // 9: 1/8T
    2.0f / 3.0f,    // 10: 1/4. (dotted)
    4.0f / 3.0f     // 11: 1/8.
};

// ---------------------------------------------------------------------------
// Waveform indices (matches OscillatorFixedPoint::WaveformType)
// ---------------------------------------------------------------------------
enum {
    kWaveformSine = 0,
    kWaveformSquare,
    kWaveformTriangle,
    kWaveformSaw,
    kWaveformMorph,
    kWaveformPolyBlepSaw,
    kWaveformPolyBlepSquare,
    kWaveformWavetable,
    kWaveformNoise
};
