// =============================================================================
// PolyLofiPresets.h — Internal preset bank for PolyLofi
// =============================================================================
// 14 factory preset slots, all overwritable. Stored in DTC, persisted via
// JSON serialise/deserialise. Presets are name-keyed for forward compatibility.
//
// Factory presets inspired by iconic 90s–2000s VA synth sounds:
//   0: Supersaw     — JP-8000 style detuned saw stack
//   1: Acid Bass    — TB-303 diode ladder resonant acid bass
//   2: Virus Lead   — Access Virus aggressive detuned lead
//   3: PWM Pad      — Prophet/Juno PWM string pad
//   4: Hoover       — Classic rave hoover bass
//   5: Fizzy Keys   — DX-style EP with noise character
//   6: Rez Sweep    — MS-20 resonant filter sweep
//   7: Sync Lead    — Hard sync screaming lead
//   8: Lofior       — Ambient comb-delay drone pad
//   9: Crushed      — Lo-fi bitcrushed texture
//  10: Moog Bass    — Classic Moog ladder bass
//  11: Tape Piano   — TZFM bell + tape echo + vinyl crackle
//  12: Scream Lead  — MS-20 screaming lead with saturated resonance
//  13: 303 Acid     — Diode ladder acid squelch
// =============================================================================
#pragma once

#include "PolyLofiParams.h"
#include <cstring>
#include <cstdio>

static constexpr int kNumPresets     = 14;
static constexpr int kNumSynthParams = kParamLoadPreset;  // params 0..kNumSynthParams-1

struct PresetSlot {
    char    name[24];
    int16_t values[kNumSynthParams];
    bool    occupied;
};

struct PresetBank {
    PresetSlot slots[kNumPresets];

    void initDefaults() {
        for (int i = 0; i < kNumPresets; ++i) {
            snprintf(slots[i].name, sizeof(slots[i].name), "%d", i + 1);
            slots[i].occupied = false;
            memset(slots[i].values, 0, sizeof(slots[i].values));
        }
    }
};

// ---------------------------------------------------------------------------
// Factory preset initialization — call after initDefaults() in construct().
// Fills all 10 slots with parameter defaults, then patches per-preset values.
// ---------------------------------------------------------------------------
inline void initFactoryPresets(PresetBank& bank, const _NT_parameter* params) {
    // Fill every slot with parameter defaults
    for (int s = 0; s < kNumPresets; ++s) {
        for (int i = 0; i < kNumSynthParams; ++i)
            bank.slots[s].values[i] = params[i].def;
        bank.slots[s].occupied = true;
    }

    // Patch macro — concise per-preset overrides
    #define P(s, p, v) bank.slots[s].values[p] = (int16_t)(v)
    #define NAME(s, n) strncpy(bank.slots[s].name, n, sizeof(bank.slots[s].name) - 1)

    // =================================================================
    // Slot 0: "Supersaw" — JP-8000 inspired trance lead/pad
    // 3 detuned PolyBLEP saws, LP4, wide stereo, subtle delay
    // =================================================================
    NAME(0, "Supersaw");
    P(0, kParamOsc1Waveform, 5);        // PolyBLEP Saw
    P(0, kParamOsc2Waveform, 5);
    P(0, kParamOsc3Waveform, 5);
    P(0, kParamOsc2Fine, -15);           // ±15 cent detune
    P(0, kParamOsc3Fine, 15);
    P(0, kParamFilterMode, 1);           // LP4
    P(0, kParamBaseCutoff, 7000);        // ~2.5 kHz
    P(0, kParamResonance, 200);
    P(0, kParamFilterEnvAmount, 3000);
    P(0, kParamAmpAttack, 5);            // 5 ms
    P(0, kParamAmpDecay, 200);
    P(0, kParamAmpSustain, 900);
    P(0, kParamAmpRelease, 300);
    P(0, kParamFilterAttack, 5);
    P(0, kParamFilterDecay, 300);
    P(0, kParamFilterSustain, 500);
    P(0, kParamFilterRelease, 300);
    P(0, kParamPanSpread, 500);
    P(0, kParamDelayTime, 375);          // 375 ms
    P(0, kParamDelayFeedback, 300);
    P(0, kParamDelayMix, 150);

    // =================================================================
    // Slot 1: "Acid Bass" — TB-303 resonant acid bass (Diode ladder)
    // Single saw, Diode LP4, screaming resonance, short filter env, glide
    // =================================================================
    NAME(1, "Acid Bass");
    P(1, kParamOsc1Waveform, 5);        // PolyBLEP Saw
    P(1, kParamOsc1Level, 700);          // single osc loud
    P(1, kParamOsc2Level, 0);            // osc 2 off
    P(1, kParamOsc3Level, 0);            // osc 3 off
    P(1, kParamFilterModel, 3);          // Diode
    P(1, kParamFilterMode, 1);           // LP4
    P(1, kParamBaseCutoff, 3000);        // ~160 Hz — closed
    P(1, kParamResonance, 800);          // screaming
    P(1, kParamFilterEnvAmount, 8000);   // massive sweep
    P(1, kParamFilterAttack, 2);
    P(1, kParamFilterDecay, 150);        // 150 ms snap
    P(1, kParamFilterSustain, 100);
    P(1, kParamFilterRelease, 100);
    P(1, kParamAmpAttack, 2);
    P(1, kParamAmpDecay, 200);
    P(1, kParamAmpSustain, 600);
    P(1, kParamAmpRelease, 100);
    P(1, kParamDrive, 2000);
    P(1, kParamGlideTime, 100);          // 100 ms
    P(1, kParamGlideMode, 2);            // Legato
    P(1, kParamDelayMix, 0);             // dry

    // =================================================================
    // Slot 2: "Virus Lead" — Access Virus aggressive detuned lead
    // 2 saws + 1 square, LP4, driven, LFO vibrato
    // =================================================================
    NAME(2, "Virus Lead");
    P(2, kParamOsc1Waveform, 5);        // PolyBLEP Saw
    P(2, kParamOsc1Fine, -7);
    P(2, kParamOsc1Level, 400);
    P(2, kParamOsc2Waveform, 5);        // PolyBLEP Saw
    P(2, kParamOsc2Fine, 7);
    P(2, kParamOsc2Level, 400);
    P(2, kParamOsc3Waveform, 6);        // PolyBLEP Square
    P(2, kParamOsc3Level, 200);
    P(2, kParamFilterMode, 1);           // LP4
    P(2, kParamBaseCutoff, 6000);        // ~1.2 kHz
    P(2, kParamResonance, 350);
    P(2, kParamFilterEnvAmount, 6000);
    P(2, kParamFilterAttack, 3);
    P(2, kParamFilterDecay, 400);
    P(2, kParamFilterSustain, 300);
    P(2, kParamFilterRelease, 200);
    P(2, kParamAmpAttack, 3);
    P(2, kParamAmpDecay, 500);
    P(2, kParamAmpSustain, 700);
    P(2, kParamAmpRelease, 250);
    P(2, kParamDrive, 1500);
    // LFO1 → Pitch: slow vibrato
    P(2, kParamLfoSpeed, 550);           // ~1.7 Hz
    P(2, kParamMod1Source, 1);           // LFO1
    P(2, kParamMod1Dest, 18);           // Pitch
    P(2, kParamMod1Amount, 80);          // subtle
    P(2, kParamDelayTime, 330);
    P(2, kParamDelayFeedback, 350);
    P(2, kParamDelayMix, 180);

    // =================================================================
    // Slot 3: "PWM Pad" — Prophet/Juno PWM string pad
    // 3 PolyBLEP squares, slow LFO→Morph (=PW), LP2, lush
    // =================================================================
    NAME(3, "PWM Pad");
    P(3, kParamOsc1Waveform, 6);        // PolyBLEP Square
    P(3, kParamOsc2Waveform, 6);
    P(3, kParamOsc3Waveform, 6);
    P(3, kParamOsc1Morph, 500);          // 50% duty cycle base
    P(3, kParamOsc2Morph, 500);
    P(3, kParamOsc3Morph, 500);
    P(3, kParamOsc2Fine, -5);            // slight detune
    P(3, kParamOsc3Fine, 5);
    P(3, kParamFilterMode, 0);           // LP2
    P(3, kParamBaseCutoff, 7500);        // ~3.5 kHz
    P(3, kParamResonance, 100);
    P(3, kParamFilterEnvAmount, 3000);
    P(3, kParamAmpAttack, 800);          // slow swell
    P(3, kParamAmpDecay, 500);
    P(3, kParamAmpSustain, 700);
    P(3, kParamAmpRelease, 1000);
    P(3, kParamFilterAttack, 400);
    P(3, kParamFilterDecay, 500);
    P(3, kParamFilterSustain, 600);
    P(3, kParamFilterRelease, 500);
    // LFO1 → AllMorph: slow PWM sweep (morph=PW on square waves)
    P(3, kParamLfoSpeed, 350);           // ~0.2 Hz
    P(3, kParamMod1Source, 1);           // LFO1
    P(3, kParamMod1Dest, 11);           // AllMorph (=PW on square)
    P(3, kParamMod1Amount, 600);
    P(3, kParamPanSpread, 600);
    P(3, kParamDelayTime, 400);
    P(3, kParamDelayFeedback, 250);
    P(3, kParamDelayMix, 200);

    // =================================================================
    // Slot 4: "Hoover" — Classic 90s rave hoover bass
    // Wide-detuned saws across octaves, LP4, driven, always-glide
    // =================================================================
    NAME(4, "Hoover");
    P(4, kParamOsc1Waveform, 3);        // Saw
    P(4, kParamOsc1Level, 400);
    P(4, kParamOsc2Waveform, 3);        // Saw -12 semi
    P(4, kParamOsc2Semitone, -12);
    P(4, kParamOsc2Fine, -20);
    P(4, kParamOsc2Level, 350);
    P(4, kParamOsc3Waveform, 3);        // Saw +7 semi (fifth)
    P(4, kParamOsc3Semitone, 7);
    P(4, kParamOsc3Fine, 20);
    P(4, kParamOsc3Level, 250);
    P(4, kParamFilterMode, 1);           // LP4
    P(4, kParamBaseCutoff, 6500);        // ~1.6 kHz
    P(4, kParamResonance, 200);
    P(4, kParamFilterEnvAmount, 4000);
    P(4, kParamFilterAttack, 3);
    P(4, kParamFilterDecay, 200);
    P(4, kParamFilterSustain, 400);
    P(4, kParamFilterRelease, 150);
    P(4, kParamAmpAttack, 5);
    P(4, kParamAmpDecay, 300);
    P(4, kParamAmpSustain, 800);
    P(4, kParamAmpRelease, 200);
    P(4, kParamDrive, 2500);
    P(4, kParamGlideTime, 50);           // 50 ms
    P(4, kParamGlideMode, 1);            // Always
    P(4, kParamDelayMix, 0);             // dry

    // =================================================================
    // Slot 5: "Fizzy Keys" — DX-style EP with noise character
    // Sine + octave-up sine + noise, LP2, plucky, velocity→cutoff
    // =================================================================
    NAME(5, "Fizzy Keys");
    P(5, kParamOsc1Waveform, 0);        // Sine
    P(5, kParamOsc1Level, 500);
    P(5, kParamOsc2Waveform, 0);        // Sine +12
    P(5, kParamOsc2Semitone, 12);
    P(5, kParamOsc2Level, 300);
    P(5, kParamOsc3Waveform, 8);        // Noise
    P(5, kParamOsc3Level, 100);
    P(5, kParamFilterMode, 0);           // LP2
    P(5, kParamBaseCutoff, 8000);        // ~5 kHz
    P(5, kParamResonance, 50);
    P(5, kParamFilterEnvAmount, 4000);
    P(5, kParamAmpAttack, 2);
    P(5, kParamAmpDecay, 400);           // plucky decay
    P(5, kParamAmpSustain, 300);
    P(5, kParamAmpRelease, 400);
    P(5, kParamFilterAttack, 2);
    P(5, kParamFilterDecay, 300);
    P(5, kParamFilterSustain, 200);
    P(5, kParamFilterRelease, 300);
    // Velocity → Cutoff
    P(5, kParamMod1Source, 7);           // Velocity
    P(5, kParamMod1Dest, 0);            // Cutoff
    P(5, kParamMod1Amount, 500);
    P(5, kParamDelayTime, 350);
    P(5, kParamDelayFeedback, 300);
    P(5, kParamDelayMix, 200);

    // =================================================================
    // Slot 6: "Rez Sweep" — MS-20 resonant filter sweep pad
    // Single saw, MS-20 LP4, near-self-osc resonance, huge slow filter env
    // =================================================================
    NAME(6, "Rez Sweep");
    P(6, kParamOsc1Waveform, 5);        // PolyBLEP Saw
    P(6, kParamOsc1Level, 700);
    P(6, kParamOsc2Level, 0);
    P(6, kParamOsc3Level, 0);
    P(6, kParamFilterModel, 2);          // MS-20
    P(6, kParamFilterMode, 1);           // LP4
    P(6, kParamBaseCutoff, 2500);        // ~100 Hz — very closed
    P(6, kParamResonance, 800);          // near self-oscillation
    P(6, kParamFilterEnvAmount, 9000);   // massive sweep range
    P(6, kParamFilterAttack, 200);       // slow open
    P(6, kParamFilterDecay, 1500);       // long sweep down
    P(6, kParamFilterSustain, 200);
    P(6, kParamFilterRelease, 800);
    P(6, kParamAmpAttack, 100);
    P(6, kParamAmpDecay, 2000);
    P(6, kParamAmpSustain, 700);
    P(6, kParamAmpRelease, 600);
    P(6, kParamDelayTime, 500);
    P(6, kParamDelayFeedback, 400);
    P(6, kParamDelayMix, 250);
    P(6, kParamDelayDiffusion, 500);     // spacious

    // =================================================================
    // Slot 7: "Sync Lead" — Hard sync screaming lead
    // Osc1 synced to Osc3 (+12), ModEnv sweeps slave pitch
    // =================================================================
    NAME(7, "Sync Lead");
    P(7, kParamOsc1Waveform, 5);        // Saw (sync slave)
    P(7, kParamOsc1Level, 700);
    P(7, kParamOsc2Level, 0);            // off
    P(7, kParamOsc3Waveform, 3);        // Saw (sync master)
    P(7, kParamOsc3Semitone, 12);        // octave up
    P(7, kParamOsc3Level, 0);            // silent in mix
    P(7, kParamSync3to1, 1);             // hard sync on
    P(7, kParamFilterMode, 1);           // LP4
    P(7, kParamBaseCutoff, 7500);        // ~3.5 kHz
    P(7, kParamResonance, 300);
    P(7, kParamFilterEnvAmount, 3000);
    P(7, kParamAmpAttack, 3);
    P(7, kParamAmpDecay, 600);
    P(7, kParamAmpSustain, 600);
    P(7, kParamAmpRelease, 200);
    P(7, kParamFilterAttack, 3);
    P(7, kParamFilterDecay, 300);
    P(7, kParamFilterSustain, 400);
    P(7, kParamFilterRelease, 200);
    P(7, kParamDrive, 1800);
    // ModEnv → Osc1 Pitch: sync sweep
    P(7, kParamModEnvAttack, 5);
    P(7, kParamModEnvDecay, 500);
    P(7, kParamModEnvSustain, 0);
    P(7, kParamModEnvRelease, 200);
    P(7, kParamMod1Source, 6);           // ModEnv
    P(7, kParamMod1Dest, 25);           // Osc1 Pitch
    P(7, kParamMod1Amount, 600);
    P(7, kParamDelayTime, 280);
    P(7, kParamDelayFeedback, 350);
    P(7, kParamDelayMix, 180);

    // =================================================================
    // Slot 8: "Lofior" — Ambient comb-delay drone pad
    // Noise + detuned PolyBLEP saws, LP4, pitch-tracked delay, HP FB
    // =================================================================
    NAME(8, "Lofior");
    P(8, kParamOsc1Waveform, 8);        // Noise
    // Osc1 level stays at default 333
    P(8, kParamOsc2Waveform, 5);        // PolyBLEP Saw
    P(8, kParamOsc2Fine, -3);
    P(8, kParamOsc2Level, 93);
    P(8, kParamOsc3Waveform, 5);        // PolyBLEP Saw
    P(8, kParamOsc3Fine, 3);
    P(8, kParamOsc3Level, 82);
    P(8, kParamBaseCutoff, 6239);
    P(8, kParamResonance, 467);
    P(8, kParamFilterEnvAmount, 2903);
    P(8, kParamFilterMode, 1);           // LP4
    P(8, kParamDrive, 1205);
    P(8, kParamAmpAttack, 1077);         // ~1s swell
    P(8, kParamAmpDecay, 979);
    P(8, kParamAmpSustain, 463);
    P(8, kParamAmpRelease, 261);
    P(8, kParamDelayFeedback, 967);      // near self-oscillation
    P(8, kParamDelayMix, 987);           // almost full wet
    P(8, kParamDelayFBFilter, 2);        // HP feedback
    P(8, kParamDelayFBFreq, 209);
    P(8, kParamDelayPitchTrack, 3);      // Oct +1
    P(8, kParamMod1Source, 1);           // LFO
    P(8, kParamMod1Dest, 15);           // Delay Time
    P(8, kParamMod1Amount, 1);           // tape wow
    P(8, kParamMod2Source, 7);           // Velocity
    P(8, kParamMod2Dest, 2);            // Amp Attack
    P(8, kParamMod2Amount, -779);

    // =================================================================
    // Slot 9: "Crushed" — Lo-fi bitcrushed texture
    // Saw + square + noise, bitcrush 8-bit, sample reduce, LP2
    // =================================================================
    NAME(9, "Crushed");
    P(9, kParamOsc1Waveform, 5);        // PolyBLEP Saw
    P(9, kParamOsc1Level, 500);
    P(9, kParamOsc2Waveform, 6);        // PolyBLEP Square
    P(9, kParamOsc2Level, 300);
    P(9, kParamOsc3Waveform, 8);        // Noise
    P(9, kParamOsc3Level, 0);
    P(9, kParamFilterMode, 0);           // LP2
    P(9, kParamFilterModel, 2);  
    P(9, kParamBaseCutoff, 4073);
    P(9, kParamResonance, 200);
    P(9, kParamFilterEnvAmount, 3000);
    P(9, kParamAmpAttack, 5);
    P(9, kParamAmpDecay, 300);
    P(9, kParamAmpSustain, 600);
    P(9, kParamAmpRelease, 200);
    P(9, kParamFilterAttack, 5);
    P(9, kParamFilterDecay, 250);
    P(9, kParamFilterSustain, 300);
    P(9, kParamFilterRelease, 200);
    P(9, kParamBitCrush, 8);             // 8-bit
    P(9, kParamSampleReduce, 4);         // 4x reduction
    P(9, kParamDrive, 1800);
    // Velocity → Drive
    P(9, kParamMod1Source, 7);           // Velocity
    P(9, kParamMod1Dest, 19);           // Drive
    P(9, kParamMod1Amount, 400);

    P(9, kParamMod2Source, 7);           // Velocity
    P(9, kParamMod2Dest, 15);           // Delay Time          
    P(9, kParamMod2Amount, 160);

    

    // ModEnv → Osc3 Level
    P(9, kParamMod3Source, 6);           // ModEnv
    P(9, kParamMod3Dest, 23);           // Osc3Level
    P(9, kParamMod3Amount, 1000);
    P(9, kParamModEnvAttack, 0);
    P(9, kParamModEnvDecay, 813);
    P(9, kParamModEnvSustain, 0);
    P(9, kParamModEnvShape, -855);
    P(9, kParamDelayTime, 300);
    P(9, kParamDelayFeedback, 400);
    P(9, kParamDelayMix, 523);
    P(9, kParamDelayDiffusion, 605);
    P(9, kParamDelayFBFilter, 1);        // LP feedback
    P(9, kParamDelayFBFreq, 3000);
    P(9, kParamRightOutput, 14);
    P(9, kParamPanSpread, 676);

    // =================================================================
    // Slot 10: "Moog Bass" — Classic Moog ladder bass
    // Single saw through ladder LP4, high resonance, key tracking,
    // punchy filter env, drive for warmth
    // =================================================================
    NAME(10, "Moog Bass");
    P(10, kParamOsc1Waveform, 5);        // PolyBLEP Saw
    P(10, kParamOsc1Level, 800);
    P(10, kParamOsc2Waveform, 6);        // PolyBLEP Square
    P(10, kParamOsc2Semitone, -12);       // sub octave
    P(10, kParamOsc2Morph, 500);          // 50% PW
    P(10, kParamOsc2Level, 400);
    P(10, kParamOsc3Level, 0);
    P(10, kParamFilterModel, 1);          // Ladder
    P(10, kParamFilterMode, 1);           // LP4
    P(10, kParamBaseCutoff, 2000);
    P(10, kParamResonance, 600);          // juicy resonance
    P(10, kParamFilterEnvAmount, 6000);
    P(10, kParamKeyboardTracking, 500);   // 50% key tracking
    P(10, kParamDrive, 2000);             // warm saturation
    P(10, kParamAmpAttack, 3);
    P(10, kParamAmpDecay, 300);
    P(10, kParamAmpSustain, 700);
    P(10, kParamAmpRelease, 150);
    P(10, kParamFilterAttack, 3);
    P(10, kParamFilterDecay, 250);
    P(10, kParamFilterSustain, 200);
    P(10, kParamFilterRelease, 150);
    P(10, kParamGlideTime, 80);           // subtle glide
    P(10, kParamGlideMode, 1);            // always
    // Velocity → Cutoff for expressive playing
    P(10, kParamMod1Source, 7);           // Velocity
    P(10, kParamMod1Dest, 0);            // Cutoff
    P(10, kParamMod1Amount, 400);

    // =================================================================
    // Slot 11: "Tape Piano" — TZFM bell + tape echo + vinyl crackle
    // Sine carrier + sine FM modulator, mod env → FM depth,
    // tape delay with LFO wobble, S&H noise crackle layer
    // =================================================================
    NAME(11, "Tape Piano");
    P(11, kParamOsc1Waveform, 0);        // Sine carrier
    P(11, kParamOsc1Level, 500);
    P(11, kParamOsc2Waveform, 8);        // Noise (crackle layer)
    P(11, kParamOsc2Level, 0);            // silent until S&H modulates
    P(11, kParamOsc3Waveform, 0);        // Sine FM modulator
    P(11, kParamOsc3Semitone, 7);         // fifth up — bell ratio
    P(11, kParamOsc3Level, 0);            // silent as audio
    P(11, kParamFM3to1, 70);              // gentle FM depth
    // Amp: piano-like fast attack, medium decay
    P(11, kParamAmpAttack, 5);
    P(11, kParamAmpDecay, 800);
    P(11, kParamAmpSustain, 200);
    P(11, kParamAmpRelease, 500);
    // Mod env: FM depth decay (bell → pure sine)
    P(11, kParamModEnvAttack, 0);
    P(11, kParamModEnvDecay, 400);
    P(11, kParamModEnvSustain, 0);
    P(11, kParamModEnvRelease, 200);
    P(11, kParamModEnvShape, -800);
    // Mod1: Mod Env → FM 3>1 depth
    P(11, kParamMod1Source, 6);           // Mod Env
    P(11, kParamMod1Dest, 13);           // FM 3>1
    P(11, kParamMod1Amount, 25);
    // Filter: gentle LP
    P(11, kParamBaseCutoff, 6000);
    P(11, kParamFilterEnvAmount, 2000);
    P(11, kParamFilterAttack, 5);
    P(11, kParamFilterDecay, 600);
    P(11, kParamFilterSustain, 300);
    P(11, kParamFilterRelease, 400);
    // Delay: tape echo with wobble
    P(11, kParamDelayTime, 350);
    P(11, kParamDelayFeedback, 500);
    P(11, kParamDelayMix, 300);
    P(11, kParamDelayDiffusion, 200);
    P(11, kParamDelayFBFilter, 1);        // LP feedback
    P(11, kParamDelayFBFreq, 2000);
    // LFO1: slow sine for tape wobble
    P(11, kParamLfoSpeed, 300);
    P(11, kParamLfoShape, 0);             // sine
    P(11, kParamMod2Source, 1);           // LFO1
    P(11, kParamMod2Dest, 15);           // Delay Time
    P(11, kParamMod2Amount, 500);
    // LFO3: S&H for vinyl crackle → Osc2 level
    P(11, kParamLfo3Speed, 700);
    P(11, kParamLfo3Shape, 5);            // S&H
    P(11, kParamLfo3Unipolar, 1);
    P(11, kParamMod3Source, 3);           // LFO3
    P(11, kParamMod3Dest, 22);           // Osc2 Level
    P(11, kParamMod3Amount, 300);

    // =================================================================
    // Slot 12: "Scream Lead" — MS-20 screaming lead
    // Pure saw through MS-20 LP4, aggressive resonance + drive,
    // ModEnv sweeps cutoff for attack scream, LFO vibrato
    // =================================================================
    NAME(12, "Scream Lead");
    P(12, kParamOsc1Waveform, 5);        // PolyBLEP Saw
    P(12, kParamOsc1Level, 800);
    P(12, kParamOsc2Waveform, 6);        // PolyBLEP Square
    P(12, kParamOsc2Fine, -8);
    P(12, kParamOsc2Level, 300);
    P(12, kParamOsc3Level, 0);
    P(12, kParamFilterModel, 2);          // MS-20
    P(12, kParamFilterMode, 1);           // LP4
    P(12, kParamBaseCutoff, 4000);
    P(12, kParamResonance, 700);          // screaming
    P(12, kParamFilterEnvAmount, 7000);
    P(12, kParamDrive, 3000);             // aggressive
    P(12, kParamAmpAttack, 3);
    P(12, kParamAmpDecay, 400);
    P(12, kParamAmpSustain, 700);
    P(12, kParamAmpRelease, 200);
    P(12, kParamFilterAttack, 5);
    P(12, kParamFilterDecay, 400);
    P(12, kParamFilterSustain, 300);
    P(12, kParamFilterRelease, 200);
    // LFO1 → Pitch: vibrato
    P(12, kParamLfoSpeed, 500);
    P(12, kParamMod1Source, 1);           // LFO1
    P(12, kParamMod1Dest, 18);           // Pitch
    P(12, kParamMod1Amount, 100);
    P(12, kParamDelayTime, 300);
    P(12, kParamDelayFeedback, 350);
    P(12, kParamDelayMix, 180);

    // =================================================================
    // Slot 13: "303 Acid" — Diode ladder acid squelch
    // PolyBLEP Square, Diode LP4, huge filter env sweep,
    // legato glide, drive for squelch
    // =================================================================
    NAME(13, "303 Acid");
    P(13, kParamOsc1Waveform, 6);        // PolyBLEP Square
    P(13, kParamOsc1Morph, 500);          // 50% PW
    P(13, kParamOsc1Level, 700);
    P(13, kParamOsc2Level, 0);
    P(13, kParamOsc3Level, 0);
    P(13, kParamFilterModel, 3);          // Diode
    P(13, kParamFilterMode, 1);           // LP4
    P(13, kParamBaseCutoff, 2500);        // closed
    P(13, kParamResonance, 750);          // squelchy
    P(13, kParamFilterEnvAmount, 8000);   // massive sweep
    P(13, kParamDrive, 2500);             // acid crunch
    P(13, kParamAmpAttack, 2);
    P(13, kParamAmpDecay, 200);
    P(13, kParamAmpSustain, 500);
    P(13, kParamAmpRelease, 100);
    P(13, kParamFilterAttack, 2);
    P(13, kParamFilterDecay, 200);
    P(13, kParamFilterSustain, 100);
    P(13, kParamFilterRelease, 100);
    P(13, kParamGlideTime, 60);
    P(13, kParamGlideMode, 2);            // Legato
    P(13, kParamDelayTime, 375);
    P(13, kParamDelayFeedback, 300);
    P(13, kParamDelayMix, 150);

    #undef P
    #undef NAME
}
