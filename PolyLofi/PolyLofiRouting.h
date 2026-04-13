// =============================================================================
// PolyLofiRouting.h — Parameter routing functions for PolyLofi
// =============================================================================
// Decomposes the 500-line parameterChanged() switch into focused routing
// functions grouped by subsystem.  Each function: scale raw param value →
// store in DTC → fan out to voices.
// =============================================================================
#pragma once

#include <distingnt/api.h>
#include <cmath>
#include "CheapMaths.h"
#include "PolyLofiPresets.h"

struct _polyLofiAlgorithm_DTC;
class  PolyLofiVoice;

// Forward declarations for clock-sync helpers (defined in PolyLofi.cpp).
static void updateSyncedLfoSpeeds(_polyLofiAlgorithm_DTC* dtc);
static void updateSyncedDelayTime(_polyLofiAlgorithm_DTC* dtc);

// ---------------------------------------------------------------------------
// Broadcast helper — avoids repeating the for-loop everywhere
// ---------------------------------------------------------------------------
#define BROADCAST(field, value) \
    do { for (int _i = 0; _i < dtc->numVoices; ++_i) dtc->voices[_i]->field = (value); } while(0)

// =========================================================================
// Filter routing
// =========================================================================
inline void routeFilter(_polyLofiAlgorithm_DTC* dtc, int p, int16_t raw) {
    switch (p) {
        case kParamBaseCutoff: {
            float normalized = raw / 10000.0f;
            dtc->baseCutoff = 20.0f * powf(1000.0f, normalized);
            BROADCAST(baseCutoff, dtc->baseCutoff);
            break;
        }
        case kParamResonance:
            dtc->resonance = raw / 1000.0f;
            BROADCAST(resonance, dtc->resonance);
            break;
        case kParamFilterEnvAmount:
            dtc->filterEnvAmount = raw;
            BROADCAST(filterEnvAmount, dtc->filterEnvAmount);
            break;
        case kParamFilterMode:
            dtc->filterMode = raw;
            BROADCAST(filterMode, dtc->filterMode);
            break;
        case kParamFilterModel:
            dtc->filterModel = raw;
            for (int i = 0; i < dtc->numVoices; ++i) {
                dtc->voices[i]->setFilterModel(static_cast<FilterModel>(raw));
            }
            break;
        case kParamDrive:
            dtc->drive = raw / 1000.0f;
            BROADCAST(drive, dtc->drive);
            break;
        case kParamFilterShape:
            dtc->filterShape = raw / 1000.0f;
            for (int i = 0; i < dtc->numVoices; ++i)
                dtc->voices[i]->setFilterShape(dtc->filterShape);
            break;
        case kParamKeyboardTracking:
            dtc->keyboardTracking = raw / 1000.0f;
            BROADCAST(keyboardTracking, dtc->keyboardTracking);
            break;
        default: break;
    }
}

// =========================================================================
// Oscillator routing (handles all 3 oscillators)
// =========================================================================
inline void routeOscillator(_polyLofiAlgorithm_DTC* dtc, int p, int16_t raw) {
    // Determine which oscillator and which sub-parameter
    int oscIdx = -1;
    int base = -1;

    if (p >= kParamOsc1Waveform && p <= kParamOsc1Level) {
        oscIdx = 0; base = kParamOsc1Waveform;
    } else if (p >= kParamOsc2Waveform && p <= kParamOsc2Level) {
        oscIdx = 1; base = kParamOsc2Waveform;
    } else if (p >= kParamOsc3Waveform && p <= kParamOsc3Level) {
        oscIdx = 2; base = kParamOsc3Waveform;
    } else {
        return;
    }

    // Common Waveform/Semi/Fine/Morph/Level handling
    int offset = p - base;
    switch (offset) {
        case 0: dtc->oscWaveform[oscIdx] = raw;            break; // Waveform
        case 1: dtc->oscSemitone[oscIdx] = raw;            break; // Semitone
        case 2: dtc->oscFine[oscIdx]     = raw;            break; // Fine
        case 3: dtc->oscMorph[oscIdx]    = raw / 1000.0f;  break; // Morph
        case 4: dtc->oscLevel[oscIdx]    = raw / 1000.0f;  break; // Level
        default: return;
    }
    for (int i = 0; i < dtc->numVoices; ++i)
        dtc->voices[i]->setOscillatorParameters(
            oscIdx,
            dtc->oscWaveform[oscIdx],
            dtc->oscSemitone[oscIdx],
            dtc->oscFine[oscIdx],
            dtc->oscMorph[oscIdx],
            dtc->oscLevel[oscIdx]);
}

// =========================================================================
// Envelope routing (Amp, Filter, Mod)
// =========================================================================
inline void routeEnvelope(_polyLofiAlgorithm_DTC* dtc, int p, int16_t raw) {
    switch (p) {
        // Amp envelope
        case kParamAmpAttack:
            dtc->ampA = raw / 1000.0f;
            for (int i = 0; i < dtc->numVoices; ++i)
                dtc->voices[i]->setAmpEnv(dtc->ampA, dtc->ampD, dtc->ampS, dtc->ampR);
            break;
        case kParamAmpDecay:
            dtc->ampD = raw / 1000.0f;
            for (int i = 0; i < dtc->numVoices; ++i)
                dtc->voices[i]->setAmpEnv(dtc->ampA, dtc->ampD, dtc->ampS, dtc->ampR);
            break;
        case kParamAmpSustain:
            dtc->ampS = raw / 1000.0f;
            for (int i = 0; i < dtc->numVoices; ++i)
                dtc->voices[i]->setAmpEnv(dtc->ampA, dtc->ampD, dtc->ampS, dtc->ampR);
            break;
        case kParamAmpRelease:
            dtc->ampR = raw / 1000.0f;
            for (int i = 0; i < dtc->numVoices; ++i)
                dtc->voices[i]->setAmpEnv(dtc->ampA, dtc->ampD, dtc->ampS, dtc->ampR);
            break;
        case kParamAmpShape:
            dtc->ampShape = raw / 1000.0f;
            for (int i = 0; i < dtc->numVoices; ++i)
                dtc->voices[i]->setAmpShape(dtc->ampShape);
            break;

        // Filter envelope
        case kParamFilterAttack:
            dtc->filterA = raw / 1000.0f;
            for (int i = 0; i < dtc->numVoices; ++i)
                dtc->voices[i]->setFilterEnv(dtc->filterA, dtc->filterD, dtc->filterS, dtc->filterR);
            break;
        case kParamFilterDecay:
            dtc->filterD = raw / 1000.0f;
            for (int i = 0; i < dtc->numVoices; ++i)
                dtc->voices[i]->setFilterEnv(dtc->filterA, dtc->filterD, dtc->filterS, dtc->filterR);
            break;
        case kParamFilterSustain:
            dtc->filterS = raw / 1000.0f;
            for (int i = 0; i < dtc->numVoices; ++i)
                dtc->voices[i]->setFilterEnv(dtc->filterA, dtc->filterD, dtc->filterS, dtc->filterR);
            break;
        case kParamFilterRelease:
            dtc->filterR = raw / 1000.0f;
            for (int i = 0; i < dtc->numVoices; ++i)
                dtc->voices[i]->setFilterEnv(dtc->filterA, dtc->filterD, dtc->filterS, dtc->filterR);
            break;

        // Mod envelope
        case kParamModEnvAttack:
            dtc->modA = raw / 1000.0f;
            for (int i = 0; i < dtc->numVoices; ++i)
                dtc->voices[i]->setModEnv(dtc->modA, dtc->modD, dtc->modS, dtc->modR);
            break;
        case kParamModEnvDecay:
            dtc->modD = raw / 1000.0f;
            for (int i = 0; i < dtc->numVoices; ++i)
                dtc->voices[i]->setModEnv(dtc->modA, dtc->modD, dtc->modS, dtc->modR);
            break;
        case kParamModEnvSustain:
            dtc->modS = raw / 1000.0f;
            for (int i = 0; i < dtc->numVoices; ++i)
                dtc->voices[i]->setModEnv(dtc->modA, dtc->modD, dtc->modS, dtc->modR);
            break;
        case kParamModEnvRelease:
            dtc->modR = raw / 1000.0f;
            for (int i = 0; i < dtc->numVoices; ++i)
                dtc->voices[i]->setModEnv(dtc->modA, dtc->modD, dtc->modS, dtc->modR);
            break;
        case kParamModEnvShape:
            dtc->modShape = raw / 1000.0f;
            for (int i = 0; i < dtc->numVoices; ++i)
                dtc->voices[i]->setModShape(dtc->modShape);
            break;
        default: break;
    }
}

// =========================================================================
// Delay routing
// =========================================================================
inline void routeDelay(_polyLofiAlgorithm_DTC* dtc, _NT_algorithm* self, int p, int16_t raw) {
    switch (p) {
        case kParamDelayTime:
            dtc->delayTimeMs = raw;
            if (dtc->delaySyncMode == 0) {
                dtc->delaySamples = dtc->delayTimeMs * 0.001f * NT_globals.sampleRate;
                BROADCAST(delaySamples, dtc->delaySamples);
            }
            break;
        case kParamDelayFeedback:
            dtc->delayFeedback = raw / 1000.0f;
            BROADCAST(delayFeedback, dtc->delayFeedback);
            break;
        case kParamDelayMix:
            dtc->delayMix = raw / 1000.0f;
            BROADCAST(delayMix, dtc->delayMix);
            break;
        case kParamDelayDiffusion:
            dtc->delayDiffusion = raw / 1000.0f;
            for (int i = 0; i < dtc->numVoices; ++i)
                dtc->voices[i]->setDelayDiffusion(dtc->delayDiffusion);
            break;
        case kParamDelayFBFilter:
            dtc->delayFBFilterMode = raw;
            for (int i = 0; i < dtc->numVoices; ++i)
                dtc->voices[i]->voiceDelay.setFeedbackFilter(
                    dtc->delayFBFilterMode, dtc->delayFBFreq, NT_globals.sampleRate);
            break;
        case kParamDelayFBFreq:
            dtc->delayFBFreq = static_cast<float>(raw);
            for (int i = 0; i < dtc->numVoices; ++i)
                dtc->voices[i]->voiceDelay.setFeedbackFilter(
                    dtc->delayFBFilterMode, dtc->delayFBFreq, NT_globals.sampleRate);
            break;
        case kParamDelayPitchTrack:
            dtc->delayPitchTrackMode = raw;
            BROADCAST(delayPitchTrackMode, dtc->delayPitchTrackMode);
            {
                bool tracked = (dtc->delayPitchTrackMode > 0);
                NT_setParameterGrayedOut(NT_algorithmIndex(self),
                    kParamDelayTime + NT_parameterOffset(), tracked);
                NT_setParameterGrayedOut(NT_algorithmIndex(self),
                    kParamDelaySyncMode + NT_parameterOffset(), tracked);
            }
            break;
        case kParamDelaySyncMode:
            dtc->delaySyncMode = raw;
            if (dtc->delaySyncMode > 0 && dtc->clockTracker.isActive()) {
                updateSyncedDelayTime(dtc);
            } else if (dtc->delaySyncMode == 0) {
                dtc->delaySamples = dtc->delayTimeMs * 0.001f * NT_globals.sampleRate;
                BROADCAST(delaySamples, dtc->delaySamples);
            }
            break;
        default: break;
    }
}

// =========================================================================
// LFO routing (handles all 3 LFOs)
// =========================================================================
inline void routeLfo(_polyLofiAlgorithm_DTC* dtc, int p, int16_t raw) {
    // Speed params need exponential mapping
    if (p == kParamLfoSpeed || p == kParamLfo2Speed || p == kParamLfo3Speed) {
        int idx = (p == kParamLfoSpeed) ? 0 : (p == kParamLfo2Speed) ? 1 : 2;
        float normalized = raw / 1000.0f;
        dtc->lfoSpeed[idx] = 0.01f * fast_powf(2.0f, normalized * 12.2877f);
        if (dtc->lfoSyncMode[idx] > 0 && dtc->clockTracker.isActive()) return;
        for (int i = 0; i < dtc->numVoices; ++i) {
            dtc->voices[i]->setLfoFrequency(idx, dtc->lfoSpeed[idx]);
            dtc->voices[i]->setLfoSampleHoldRate(idx, dtc->lfoSpeed[idx]);
        }
        return;
    }

    // Shape params
    if (p == kParamLfoShape || p == kParamLfo2Shape || p == kParamLfo3Shape) {
        int idx = (p == kParamLfoShape) ? 0 : (p == kParamLfo2Shape) ? 1 : 2;
        dtc->lfoShape[idx] = raw;
        for (int i = 0; i < dtc->numVoices; ++i)
            dtc->voices[i]->setLfoShape(idx, dtc->lfoShape[idx]);
        return;
    }

    // Unipolar params
    if (p == kParamLfoUnipolar || p == kParamLfo2Unipolar || p == kParamLfo3Unipolar) {
        int idx = (p == kParamLfoUnipolar) ? 0 : (p == kParamLfo2Unipolar) ? 1 : 2;
        dtc->lfoUnipolar[idx] = (raw != 0);
        for (int i = 0; i < dtc->numVoices; ++i)
            dtc->voices[i]->setLfoUnipolar(idx, dtc->lfoUnipolar[idx]);
        return;
    }

    // Morph params
    if (p == kParamLfoMorph || p == kParamLfo2Morph || p == kParamLfo3Morph) {
        int idx = (p == kParamLfoMorph) ? 0 : (p == kParamLfo2Morph) ? 1 : 2;
        dtc->lfoMorph[idx] = raw / 1000.0f;
        for (int i = 0; i < dtc->numVoices; ++i)
            dtc->voices[i]->setLfoMorph(idx, dtc->lfoMorph[idx]);
        return;
    }

    // Sync mode params
    if (p == kParamLfo1SyncMode || p == kParamLfo2SyncMode || p == kParamLfo3SyncMode) {
        int idx = (p == kParamLfo1SyncMode) ? 0 : (p == kParamLfo2SyncMode) ? 1 : 2;
        dtc->lfoSyncMode[idx] = raw;
        if (dtc->lfoSyncMode[idx] > 0 && dtc->clockTracker.getBPM() > 0.0f)
            updateSyncedLfoSpeeds(dtc);
        return;
    }

    // Key sync params
    if (p == kParamLfo1KeySync || p == kParamLfo2KeySync || p == kParamLfo3KeySync) {
        int idx = p - kParamLfo1KeySync;
        bool on = (raw != 0);
        dtc->lfoKeySync[idx] = on;
        for (int i = 0; i < dtc->numVoices; ++i)
            dtc->voices[i]->lfoKeySync[idx] = on;
        return;
    }
}

// =========================================================================
// Modulation matrix routing
// =========================================================================
inline void routeModMatrix(_polyLofiAlgorithm_DTC* dtc, int p, int16_t raw) {
    if (p >= kParamMod1Source && p <= kParamMod4Amount) {
        // Source/Dest/Amount are in groups of 3
        if (p == kParamMod1Source || p == kParamMod2Source ||
            p == kParamMod3Source || p == kParamMod4Source) {
            int slot = (p - kParamMod1Source) / 3;
            dtc->matrix[slot].sourceIdx = raw;
        } else if (p == kParamMod1Dest || p == kParamMod2Dest ||
                   p == kParamMod3Dest || p == kParamMod4Dest) {
            int slot = (p - kParamMod1Dest) / 3;
            dtc->matrix[slot].destIdx = raw;
        } else if (p == kParamMod1Amount || p == kParamMod2Amount ||
                   p == kParamMod3Amount || p == kParamMod4Amount) {
            int slot = (p - kParamMod1Amount) / 3;
            dtc->matrix[slot].amount = raw / 1000.0f;
        }
    }
}

// =========================================================================
// FM / Sync routing
// =========================================================================
inline void routeFmSync(_polyLofiAlgorithm_DTC* dtc, int p, int16_t raw) {
    switch (p) {
        case kParamFM3to2:
            dtc->fmDepth3to2 = raw;
            BROADCAST(fmDepth3to2, dtc->fmDepth3to2);
            break;
        case kParamFM3to1:
            dtc->fmDepth3to1 = raw;
            BROADCAST(fmDepth3to1, dtc->fmDepth3to1);
            break;
        case kParamFM2to1:
            dtc->fmDepth2to1 = raw;
            BROADCAST(fmDepth2to1, dtc->fmDepth2to1);
            break;
        case kParamSync3to2:
            dtc->syncEnable3to2 = (raw != 0);
            BROADCAST(syncEnable3to2, dtc->syncEnable3to2);
            break;
        case kParamSync3to1:
            dtc->syncEnable3to1 = (raw != 0);
            BROADCAST(syncEnable3to1, dtc->syncEnable3to1);
            break;
        case kParamSync2to1:
            dtc->syncEnable2to1 = (raw != 0);
            BROADCAST(syncEnable2to1, dtc->syncEnable2to1);
            break;
        default: break;
    }
}

// =========================================================================
// Miscellaneous routing (glide, bit crush, pan, wavetable, MIDI channel)
// =========================================================================
inline void routeMisc(_polyLofiAlgorithm_DTC* dtc, int p, int16_t raw) {
    switch (p) {
        case kParamMidiChannel:
            dtc->midiChannel = raw;
            break;
        case kParamGlideTime:
            dtc->glideTimeMs = raw;
            BROADCAST(glideTimeMs, dtc->glideTimeMs);
            break;
        case kParamGlideMode:
            dtc->glideMode = raw;
            BROADCAST(glideMode, dtc->glideMode);
            break;
        case kParamBitCrush:
            dtc->bitCrushBits = raw;
            BROADCAST(bitCrushBits, dtc->bitCrushBits);
            break;
        case kParamSampleReduce:
            dtc->sampleReduceFactor = raw;
            BROADCAST(sampleReduceFactor, dtc->sampleReduceFactor);
            break;
        case kParamPanSpread: {
            float spread = raw / 1000.0f;
            dtc->panSpread = spread;
            float halfN = static_cast<float>(dtc->numVoices) / 2.0f;
            for (int i = 0; i < dtc->numVoices; ++i) {
                float p_v;
                if (i == 0) {
                    p_v = 0.5f;
                } else {
                    int ring = (i + 1) / 2;
                    float side = (i % 2 == 1) ? 1.0f : -1.0f;
                    p_v = 0.5f + side * spread * static_cast<float>(ring) / halfN * 0.5f;
                }
                dtc->voices[i]->setPan(p_v);
            }
            break;
        }
        case kParamOsc1Wavetable:
        case kParamOsc2Wavetable:
        case kParamOsc3Wavetable: {
            int wtOscIdx = p - kParamOsc1Wavetable;
            dtc->wtManager.loadWavetable(wtOscIdx, raw);
            break;
        }
        case kParamMasterVolume:
            dtc->masterGain = raw * (5.0f / 100.0f);  // 0-100% → 0-5V
            break;
        case kParamLegato:
            dtc->legato = (raw != 0);
            break;
        case kParamLfo1CutoffMod:
            dtc->lfo1CutoffMod = raw / 1000.0f;  // -1.0 to +1.0
            BROADCAST(lfo1CutoffMod, dtc->lfo1CutoffMod);
            break;
        case kParamLfo2VibratoMod:
            dtc->lfo2VibratoMod = static_cast<float>(raw);  // 0-100 cents
            BROADCAST(lfo2VibratoMod, dtc->lfo2VibratoMod);
            break;
        case kParamVelocitySens:
            dtc->velocitySens = raw / 100.0f;  // 0.0 to 1.0
            BROADCAST(velocitySens, dtc->velocitySens);
            break;
        default: break;
    }
}

// =========================================================================
// Preset routing (load / save)
// =========================================================================
inline void routePreset(_polyLofiAlgorithm_DTC* dtc, _NT_algorithm* self, int p, int16_t raw) {
    switch (p) {
        case kParamLoadPreset: {
            int slot = self->v[kParamLoadPreset];
            if (slot >= 0 && slot < kNumPresets && dtc->presets.slots[slot].occupied) {
                const PresetSlot& preset = dtc->presets.slots[slot];
                int32_t algIdx = NT_algorithmIndex(self);
                uint32_t offset = NT_parameterOffset();
                for (int i = 0; i < kNumSynthParams; ++i) {
                    if (isSetupParam(i)) continue;
                    NT_setParameterFromAudio(algIdx, i + offset, preset.values[i]);
                }
            }
            break;
        }
        case kParamSavePreset:
            // Slot selector only — no action on change
            break;
        case kParamSaveConfirm: {
            if (raw == 0) break;  // Only act on rising edge (Off→On)
            int slot = self->v[kParamSavePreset];
            if (slot >= 0 && slot < kNumPresets) {
                PresetSlot& preset = dtc->presets.slots[slot];
                for (int i = 0; i < kNumSynthParams; ++i) {
                    if (isSetupParam(i)) continue;
                    preset.values[i] = self->v[i];
                }
                preset.occupied = true;
            }
            // Defer auto-reset so "On" is visible on screen (~150ms)
            dtc->saveResetCountdown = 100;
            break;
        }
        default: break;
    }
}

#undef BROADCAST
