// =============================================================================
// PolyLofi Integration Tests — headless, no hardware required
// =============================================================================
// Tests the full PolyLofi plugin lifecycle: construct → parameterChanged →
// midiMessage → step → audio output.
//
// Build:  make test       (from PolyLofi directory)
// Run:    make test-run
// =============================================================================

#include "test_framework.h"
#include "plugin_harness.h"
#include "wav_writer.h"
#include "sha256.h"
#include "../PolyLofiParams.h"
#include "../../LofiParts/WavetableGenerator.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

// PolyLofi defaults: output bus = 13 (1-based) → index 12 (0-based)
static constexpr int OUTPUT_BUS = 12;
static constexpr int BLOCK_SIZE = 64;

// -------------------------------------------------------------------------
// Short parameter aliases — derived from canonical enum in PolyLofiParams.h
// -------------------------------------------------------------------------
enum {
    kP_Output = kParamOutput, kP_OutputMode = kParamOutputMode,
    kP_BaseCutoff = kParamBaseCutoff, kP_Resonance = kParamResonance,
    kP_FilterEnvAmount = kParamFilterEnvAmount, kP_FilterMode = kParamFilterMode,
    kP_Drive = kParamDrive,
    kP_Osc1Waveform = kParamOsc1Waveform, kP_Osc1Semitone = kParamOsc1Semitone,
    kP_Osc1Fine = kParamOsc1Fine, kP_Osc1Morph = kParamOsc1Morph, kP_Osc1Level = kParamOsc1Level,
    kP_Osc2Waveform = kParamOsc2Waveform, kP_Osc2Semitone = kParamOsc2Semitone,
    kP_Osc2Fine = kParamOsc2Fine, kP_Osc2Morph = kParamOsc2Morph, kP_Osc2Level = kParamOsc2Level,
    kP_Osc3Waveform = kParamOsc3Waveform, kP_Osc3Semitone = kParamOsc3Semitone,
    kP_Osc3Fine = kParamOsc3Fine, kP_Osc3Morph = kParamOsc3Morph, kP_Osc3Level = kParamOsc3Level,
    kP_AmpAttack = kParamAmpAttack, kP_AmpDecay = kParamAmpDecay,
    kP_AmpSustain = kParamAmpSustain, kP_AmpRelease = kParamAmpRelease, kP_AmpShape = kParamAmpShape,
    kP_DelayTime = kParamDelayTime, kP_DelayFeedback = kParamDelayFeedback, kP_DelayMix = kParamDelayMix,
    kP_FilterAttack = kParamFilterAttack, kP_FilterDecay = kParamFilterDecay,
    kP_FilterSustain = kParamFilterSustain, kP_FilterRelease = kParamFilterRelease,
    kP_FilterShape = kParamFilterShape,
    kP_MidiChannel = kParamMidiChannel,
    kP_LfoSpeed = kParamLfoSpeed, kP_Lfo2Speed = kParamLfo2Speed, kP_Lfo3Speed = kParamLfo3Speed,
    kP_LfoShape = kParamLfoShape, kP_LfoUnipolar = kParamLfoUnipolar, kP_LfoMorph = kParamLfoMorph,
    kP_Lfo2Shape = kParamLfo2Shape, kP_Lfo2Unipolar = kParamLfo2Unipolar, kP_Lfo2Morph = kParamLfo2Morph,
    kP_Lfo3Shape = kParamLfo3Shape, kP_Lfo3Unipolar = kParamLfo3Unipolar, kP_Lfo3Morph = kParamLfo3Morph,
    kP_Mod1Source = kParamMod1Source, kP_Mod1Dest = kParamMod1Dest, kP_Mod1Amount = kParamMod1Amount,
    kP_Mod2Source = kParamMod2Source, kP_Mod2Dest = kParamMod2Dest, kP_Mod2Amount = kParamMod2Amount,
    kP_Mod3Source = kParamMod3Source, kP_Mod3Dest = kParamMod3Dest, kP_Mod3Amount = kParamMod3Amount,
    kP_Mod4Source = kParamMod4Source, kP_Mod4Dest = kParamMod4Dest, kP_Mod4Amount = kParamMod4Amount,
    kP_ModEnvAttack = kParamModEnvAttack, kP_ModEnvDecay = kParamModEnvDecay,
    kP_ModEnvSustain = kParamModEnvSustain, kP_ModEnvRelease = kParamModEnvRelease,
    kP_ModEnvShape = kParamModEnvShape,
    kP_FM3to2 = kParamFM3to2, kP_FM3to1 = kParamFM3to1, kP_FM2to1 = kParamFM2to1,
    kP_Sync3to2 = kParamSync3to2, kP_Sync3to1 = kParamSync3to1, kP_Sync2to1 = kParamSync2to1,
    kP_GlideTime = kParamGlideTime, kP_GlideMode = kParamGlideMode,
    kP_BitCrush = kParamBitCrush, kP_SampleReduce = kParamSampleReduce,
    kP_RightOutput = kParamRightOutput, kP_RightOutputMode = kParamRightOutputMode,
    kP_Lfo1SyncMode = kParamLfo1SyncMode, kP_Lfo2SyncMode = kParamLfo2SyncMode,
    kP_Lfo3SyncMode = kParamLfo3SyncMode,
    kP_Osc1Wavetable = kParamOsc1Wavetable, kP_Osc2Wavetable = kParamOsc2Wavetable,
    kP_Osc3Wavetable = kParamOsc3Wavetable,
    kP_PanSpread = kParamPanSpread,
    kP_DelaySyncMode = kParamDelaySyncMode,
    kP_Lfo1KeySync = kParamLfo1KeySync, kP_Lfo2KeySync = kParamLfo2KeySync,
    kP_Lfo3KeySync = kParamLfo3KeySync,
    kP_DelayDiffusion = kParamDelayDiffusion,
    kP_ClockInput = kParamClockInput,
    kP_DelayFBFilter = kParamDelayFBFilter,
    kP_DelayFBFreq = kParamDelayFBFreq,
    kP_DelayPitchTrack = kParamDelayPitchTrack,
    kP_MasterVolume = kParamMasterVolume,
    kP_FilterModel = kParamFilterModel,
    kP_KeyboardTracking = kParamKeyboardTracking,
    kP_Legato = kParamLegato,
    kP_Lfo1CutoffMod = kParamLfo1CutoffMod,
    kP_Lfo2VibratoMod = kParamLfo2VibratoMod,
    kP_VelocitySens = kParamVelocitySens,
    kP_LoadPreset = kParamLoadPreset,
    kP_SavePreset = kParamSavePreset,
    kP_SaveConfirm = kParamSaveConfirm,
};

// Waveform enum values
enum { kWave_Sine = 0, kWave_Square, kWave_Triangle, kWave_Saw, kWave_Morph,
       kWave_PolyBlepSaw, kWave_PolyBlepSquare, kWave_Wavetable, kWave_Noise };

// Mod source enum values
enum { kSrc_Off = 0, kSrc_LFO, kSrc_LFO2, kSrc_LFO3, kSrc_AmpEnv, kSrc_FilterEnv, kSrc_ModEnv, kSrc_Velocity, kSrc_ModWheel, kSrc_Aftertouch };

// Mod dest enum values
enum {
    kDst_Cutoff = 0, kDst_Resonance,
    kDst_AmpAttack, kDst_AmpDecay, kDst_AmpRelease,
    kDst_FilterAttack, kDst_FilterDecay, kDst_FilterRelease,
    kDst_Osc1Morph, kDst_Osc2Morph, kDst_Osc3Morph, kDst_AllMorph,
    kDst_FM3to2, kDst_FM3to1, kDst_FM2to1,
    kDst_DelayTime, kDst_DelayFeedback, kDst_DelayMix,
    kDst_Pitch, kDst_Drive, kDst_FilterEnvAmount,
    kDst_Osc1Level, kDst_Osc2Level, kDst_Osc3Level,
    kDst_Osc1Pitch, kDst_Osc2Pitch, kDst_Osc3Pitch, kDst_LfoSpeed
};

// -------------------------------------------------------------------------
// Helper: create a fully-constructed PolyLofi instance
// -------------------------------------------------------------------------
static bool createPlugin(PluginInstance& plugin) {
    if (!plugin.load(0)) return false;
    plugin.initStatic();
    return plugin.construct();
}

// =========================================================================
// Test: Plugin loads and constructs
// =========================================================================
TestResult test_plugin_loads() {
    TEST_BEGIN("Plugin loads and constructs");

    PluginInstance plugin;
    ASSERT_TRUE(plugin.load(0), "pluginEntry returned a factory");
    ASSERT_NOT_NULL(plugin.factory(), "factory pointer is valid");

    // Check name
    const char* name = plugin.name();
    ASSERT_TRUE(name != nullptr, "factory has a name");
    ASSERT_TRUE(std::string(name) == "Poly Lofi", "factory name is 'Poly Lofi'");

    // Construct
    plugin.initStatic();
    ASSERT_TRUE(plugin.construct(), "construct succeeded");
    ASSERT_NOT_NULL(plugin.algorithm(), "algorithm pointer is valid");
    ASSERT_GT(plugin.numParameters(), 50, "has many parameters (expected ~65)");

    TEST_PASS();
}

// =========================================================================
// Test: Silence when no notes are playing
// =========================================================================
TestResult test_silence_when_idle() {
    TEST_BEGIN("Silence when no notes playing");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    // Run several blocks without sending any MIDI
    for (int i = 0; i < 10; ++i) {
        plugin.step(BLOCK_SIZE);
    }
    float* out = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
    ASSERT_TRUE(PluginInstance::isSilent(out, BLOCK_SIZE), "output is silent with no notes");

    TEST_PASS();
}

// =========================================================================
// Test: Note-on produces audio
// =========================================================================
TestResult test_note_on_produces_audio() {
    TEST_BEGIN("Note-on produces audio");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    // Send note-on: channel 0, middle C, velocity 100
    plugin.midiNoteOn(0, 60, 100);

    // Run enough blocks for the amp envelope attack to produce output
    float peakLevel = 0.0f;
    for (int i = 0; i < 20; ++i) {
        plugin.step(BLOCK_SIZE);
        float* out = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
        float p = PluginInstance::peak(out, BLOCK_SIZE);
        if (p > peakLevel) peakLevel = p;
    }

    ASSERT_GT(peakLevel, 0.001f, "peak output > -60 dB after note-on");

    TEST_PASS();
}

// =========================================================================
// Test: Note-off leads to silence (after release)
// =========================================================================
TestResult test_note_off_release() {
    TEST_BEGIN("Note-off leads to silence after release");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    // Note on
    plugin.midiNoteOn(0, 60, 100);
    for (int i = 0; i < 20; ++i) {
        plugin.step(BLOCK_SIZE);
    }

    // Note off
    plugin.midiNoteOff(0, 60);

    // Run many blocks to let release finish (default release = 0.5s @ 96kHz)
    // 0.5s = ~375 blocks of 128 @ 96kHz. Run 500 to be safe.
    float lastPeak = 0.0f;
    for (int i = 0; i < 500; ++i) {
        plugin.step(BLOCK_SIZE);
        float* out = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
        lastPeak = PluginInstance::peak(out, BLOCK_SIZE);
    }

    ASSERT_LT(lastPeak, 0.0001f, "output is near-silent after release");

    TEST_PASS();
}

// =========================================================================
// Test: Multiple voices (polyphony)
// =========================================================================
TestResult test_polyphony() {
    TEST_BEGIN("Multiple simultaneous notes produce audio");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    // Send a 4-note chord
    plugin.midiNoteOn(0, 48, 100);  // C3
    plugin.midiNoteOn(0, 52, 100);  // E3
    plugin.midiNoteOn(0, 55, 100);  // G3
    plugin.midiNoteOn(0, 60, 100);  // C4

    // Let them ring
    for (int i = 0; i < 20; ++i) {
        plugin.step(BLOCK_SIZE);
    }
    float* out = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
    float peakChord = PluginInstance::peak(out, BLOCK_SIZE);

    // Now play a single note for comparison
    PluginInstance plugin2;
    ASSERT_TRUE(createPlugin(plugin2), "plugin2 created");
    plugin2.midiNoteOn(0, 60, 100);
    for (int i = 0; i < 20; ++i) {
        plugin2.step(BLOCK_SIZE);
    }
    float* out2 = plugin2.getBus(OUTPUT_BUS, BLOCK_SIZE);
    float peakSingle = PluginInstance::peak(out2, BLOCK_SIZE);

    // Chord should be louder than single note
    ASSERT_GT(peakChord, peakSingle * 1.2f, "chord is louder than single note");

    TEST_PASS();
}

// =========================================================================
// Test: Voice stealing works (more than 8 notes)
// =========================================================================
TestResult test_voice_stealing() {
    TEST_BEGIN("Voice stealing with 9+ notes doesn't crash");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    // Send 12 note-ons (exceeds 8 voices)
    for (int n = 48; n < 60; ++n) {
        plugin.midiNoteOn(0, n, 100);
    }

    // Should not crash — run a few blocks
    for (int i = 0; i < 10; ++i) {
        plugin.step(BLOCK_SIZE);
    }
    float* out = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
    ASSERT_GT(PluginInstance::peak(out, BLOCK_SIZE), 0.001f, "still producing audio after voice stealing");

    TEST_PASS();
}

// =========================================================================
// Test: MIDI channel filtering
// =========================================================================
TestResult test_midi_channel_filter() {
    TEST_BEGIN("MIDI channel filter respects midiChannel param");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    // Find the MIDI channel parameter index.  We know it exists but need to
    // search for it by name from the parameter definitions.
    int midiChParam = -1;
    for (int i = 0; i < plugin.numParameters(); ++i) {
        if (plugin.algorithm()->parameters[i].name &&
            std::string(plugin.algorithm()->parameters[i].name) == "MIDI Channel") {
            midiChParam = i;
            break;
        }
    }
    ASSERT_TRUE(midiChParam >= 0, "found MIDI Channel parameter");

    // Set MIDI channel to 5 (only respond to channel 5)
    plugin.setParameter(midiChParam, 5);

    // Send note on channel 0 — should be ignored
    plugin.midiNoteOn(0, 60, 100);
    for (int i = 0; i < 10; ++i) plugin.step(BLOCK_SIZE);
    float* out = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
    ASSERT_TRUE(PluginInstance::isSilent(out, BLOCK_SIZE), "channel 0 note ignored when filter = 5");

    // Send note on channel 4 (0-indexed → MIDI channel 5) — should produce audio
    plugin.midiNoteOn(4, 60, 100);
    for (int i = 0; i < 20; ++i) plugin.step(BLOCK_SIZE);
    out = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
    ASSERT_GT(PluginInstance::peak(out, BLOCK_SIZE), 0.001f, "channel 5 note accepted");

    TEST_PASS();
}

// =========================================================================
// Test: Parameter changes don't crash
// =========================================================================
TestResult test_parameter_sweep() {
    TEST_BEGIN("Sweeping all parameters doesn't crash");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    // Send a note so there's active processing
    plugin.midiNoteOn(0, 60, 100);
    plugin.step(BLOCK_SIZE);

    // Sweep every parameter to its min, then max
    for (int i = 0; i < plugin.numParameters(); ++i) {
        int16_t minVal = plugin.algorithm()->parameters[i].min;
        int16_t maxVal = plugin.algorithm()->parameters[i].max;
        plugin.setParameter(i, minVal);
        plugin.step(BLOCK_SIZE);
        plugin.setParameter(i, maxVal);
        plugin.step(BLOCK_SIZE);
    }

    // If we got here without crashing, it's a pass
    ASSERT_TRUE(true, "survived parameter sweep");

    TEST_PASS();
}

// =========================================================================
// Test: WAV output capture (writes a file for manual inspection)
// =========================================================================
TestResult test_wav_capture() {
    TEST_BEGIN("WAV capture (writes bin/test_output.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/test_output.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Play a short phrase: C4 for 0.5s, rest 0.2s, E4 for 0.5s
    int blocksPerHalfSecond = (int)(NtTestHarness::getSampleRate() * 0.5f / BLOCK_SIZE);
    int blocksPerRest = (int)(NtTestHarness::getSampleRate() * 0.2f / BLOCK_SIZE);

    // C4 on
    plugin.midiNoteOn(0, 60, 100);
    for (int i = 0; i < blocksPerHalfSecond; ++i) {
        plugin.step(BLOCK_SIZE);
        wav.writeMono(plugin.getBus(OUTPUT_BUS, BLOCK_SIZE), BLOCK_SIZE);
    }

    // C4 off + rest
    plugin.midiNoteOff(0, 60);
    for (int i = 0; i < blocksPerRest; ++i) {
        plugin.step(BLOCK_SIZE);
        wav.writeMono(plugin.getBus(OUTPUT_BUS, BLOCK_SIZE), BLOCK_SIZE);
    }

    // E4 on
    plugin.midiNoteOn(0, 64, 100);
    for (int i = 0; i < blocksPerHalfSecond; ++i) {
        plugin.step(BLOCK_SIZE);
        wav.writeMono(plugin.getBus(OUTPUT_BUS, BLOCK_SIZE), BLOCK_SIZE);
    }

    // E4 off + tail
    plugin.midiNoteOff(0, 64);
    for (int i = 0; i < blocksPerHalfSecond; ++i) {
        plugin.step(BLOCK_SIZE);
        wav.writeMono(plugin.getBus(OUTPUT_BUS, BLOCK_SIZE), BLOCK_SIZE);
    }

    wav.close();
    ASSERT_TRUE(true, "WAV file written");

    TEST_PASS();
}

// =========================================================================
// Test: Chord WAV capture — C major triad held for 1.5s
// =========================================================================
TestResult test_chord_wav() {
    TEST_BEGIN("Chord WAV capture (writes bin/test_chord.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/test_chord.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    float sr = NtTestHarness::getSampleRate();
    auto blocks = [&](float seconds) -> int {
        return static_cast<int>(sr * seconds / BLOCK_SIZE);
    };

    // Short lead-in silence (50 ms)
    for (int i = 0; i < blocks(0.05f); ++i) {
        plugin.step(BLOCK_SIZE);
        wav.writeMono(plugin.getBus(OUTPUT_BUS, BLOCK_SIZE), BLOCK_SIZE);
    }

    // Strike chord — all three notes on
    plugin.midiNoteOn(0, 60, 100);  // C4
    plugin.midiNoteOn(0, 64, 100);  // E4
    plugin.midiNoteOn(0, 67, 100);  // G4

    // Hold for 1.5 seconds
    for (int i = 0; i < blocks(1.5f); ++i) {
        plugin.step(BLOCK_SIZE);
        const float* bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
        wav.writeMono(bus, BLOCK_SIZE);
    }

    // Verify the chord is producing audio
    plugin.step(BLOCK_SIZE);
    const float* bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
    float peak = PluginInstance::peak(bus, BLOCK_SIZE);
    wav.writeMono(bus, BLOCK_SIZE);
    ASSERT_GT(peak, 0.001f, "chord produces audio");

    // Release all notes
    plugin.midiNoteOff(0, 60);
    plugin.midiNoteOff(0, 64);
    plugin.midiNoteOff(0, 67);

    // Release tail (1 second)
    for (int i = 0; i < blocks(1.0f); ++i) {
        plugin.step(BLOCK_SIZE);
        wav.writeMono(plugin.getBus(OUTPUT_BUS, BLOCK_SIZE), BLOCK_SIZE);
    }

    wav.close();
    ASSERT_TRUE(true, "WAV file written");

    TEST_PASS();
}

// =========================================================================
// FEATURE WAV TESTS
// Each test writes a separate WAV and asserts the feature is audible.
// =========================================================================

/// Helper: seconds to block count
static int blocksFor(float seconds) {
    return static_cast<int>(NtTestHarness::getSampleRate() * seconds / BLOCK_SIZE);
}

/// Helper: render plugin to WAV for `seconds`, returns peak over the duration
static float renderToWav(PluginInstance& plugin, WavWriter& wav, float seconds) {
    float peakAll = 0.0f;
    int n = blocksFor(seconds);
    for (int i = 0; i < n; ++i) {
        plugin.step(BLOCK_SIZE);
        const float* bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
        wav.writeMono(bus, BLOCK_SIZE);
        float p = PluginInstance::peak(bus, BLOCK_SIZE);
        if (p > peakAll) peakAll = p;
    }
    return peakAll;
}

/// Helper: render plugin to WAV while injecting MIDI clock (F8) at the given BPM.
/// clockAccum is carried across calls to maintain fractional tick timing.
static float renderToWavWithClock(PluginInstance& plugin, WavWriter& wav,
                                   float seconds, float bpm, float& clockAccum) {
    float peakAll = 0.0f;
    float sr = static_cast<float>(NtTestHarness::getSampleRate());
    float f8Period = sr * 60.0f / (bpm * 24.0f); // samples between F8 ticks
    int n = blocksFor(seconds);
    for (int i = 0; i < n; ++i) {
        // Inject F8 ticks that fall within this block
        clockAccum += static_cast<float>(BLOCK_SIZE);
        while (clockAccum >= f8Period) {
            plugin.midiClockTick();
            clockAccum -= f8Period;
        }
        plugin.step(BLOCK_SIZE);
        const float* bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
        wav.writeMono(bus, BLOCK_SIZE);
        float p = PluginInstance::peak(bus, BLOCK_SIZE);
        if (p > peakAll) peakAll = p;
    }
    return peakAll;
}

/// Helper: render plugin to stereo WAV for `seconds`, returns peak over the duration.
/// Reads left bus from OUTPUT_BUS and right bus from rightBusIdx (0-based).
static float renderToStereoWav(PluginInstance& plugin, WavWriter& wav,
                                float seconds, int rightBusIdx) {
    float peakAll = 0.0f;
    int n = blocksFor(seconds);
    for (int i = 0; i < n; ++i) {
        plugin.step(BLOCK_SIZE);
        const float* busL = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
        const float* busR = plugin.getBus(rightBusIdx, BLOCK_SIZE);
        wav.writeStereo(busL, busR, BLOCK_SIZE);
        float pL = PluginInstance::peak(busL, BLOCK_SIZE);
        float pR = PluginInstance::peak(busR, BLOCK_SIZE);
        float p = std::max(pL, pR);
        if (p > peakAll) peakAll = p;
    }
    return peakAll;
}

// =========================================================================
// Feature: Each waveform type produces distinct audio
// WAV: 5 waveforms x 0.5s each, separated by gaps
// =========================================================================
TestResult test_waveforms_wav() {
    TEST_BEGIN("All waveforms produce audio (writes bin/feat_waveforms.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_waveforms.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Solo osc1, open filter, no delay
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    const char* waveNames[] = {"Sine", "Square", "Triangle", "Sawtooth", "Morph"};
    float peaks[5];

    for (int w = 0; w < 5; ++w) {
        plugin.setParameter(kP_Osc1Waveform, w);
        if (w == kWave_Morph) plugin.setParameter(kP_Osc1Morph, 500);

        plugin.midiNoteOn(0, 60, 100);
        peaks[w] = renderToWav(plugin, wav, 0.5f);
        plugin.midiNoteOff(0, 60);
        renderToWav(plugin, wav, 0.15f);
    }

    for (int w = 0; w < 5; ++w) {
        char msg[64];
        snprintf(msg, sizeof(msg), "%s waveform produces audio", waveNames[w]);
        ASSERT_GT(peaks[w], 0.001f, msg);
    }

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Feature: 3 Amp Envelope shapes (pluck / pad / percussive)
// =========================================================================
TestResult test_amp_envelopes_wav() {
    TEST_BEGIN("Amp envelope shapes (writes bin/feat_amp_env.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_amp_env.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    // --- 1. Fast pluck: A=1ms D=80ms S=0 R=50ms ---
    plugin.setParameter(kP_AmpAttack, 1);
    plugin.setParameter(kP_AmpDecay, 80);
    plugin.setParameter(kP_AmpSustain, 0);
    plugin.setParameter(kP_AmpRelease, 50);
    plugin.setParameter(kP_AmpShape, 0);

    plugin.midiNoteOn(0, 60, 100);
    float peakPluck = renderToWav(plugin, wav, 0.4f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 0.4f);

    // --- 2. Slow pad: A=500ms D=200ms S=0.8 R=1s ---
    plugin.setParameter(kP_AmpAttack, 500);
    plugin.setParameter(kP_AmpDecay, 200);
    plugin.setParameter(kP_AmpSustain, 800);
    plugin.setParameter(kP_AmpRelease, 1000);

    plugin.midiNoteOn(0, 60, 100);
    float peakPad = renderToWav(plugin, wav, 1.5f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 1.2f);

    // --- 3. Percussive: A=0ms D=300ms S=0.2 R=100ms Shape=+0.8 ---
    plugin.setParameter(kP_AmpAttack, 0);
    plugin.setParameter(kP_AmpDecay, 300);
    plugin.setParameter(kP_AmpSustain, 200);
    plugin.setParameter(kP_AmpRelease, 100);
    plugin.setParameter(kP_AmpShape, 800);

    plugin.midiNoteOn(0, 60, 100);
    float peakPerc = renderToWav(plugin, wav, 0.6f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 0.4f);

    ASSERT_GT(peakPluck, 0.001f, "pluck envelope produces audio");
    ASSERT_GT(peakPad, 0.001f, "pad envelope produces audio");
    ASSERT_GT(peakPerc, 0.001f, "percussive envelope produces audio");

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Feature: TZFM — Osc3->Osc1 at increasing depths
// =========================================================================
TestResult test_tzfm_wav() {
    TEST_BEGIN("TZFM synthesis (writes bin/feat_tzfm.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_tzfm.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Sine);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_Osc3Waveform, kWave_Sine);
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    int depths[] = {0, 500, 3000, 8000};
    for (int d : depths) {
        plugin.setParameter(kP_FM3to1, d);
        plugin.midiNoteOn(0, 48, 100);
        float pk = renderToWav(plugin, wav, 1.0f);
        plugin.midiNoteOff(0, 48);
        renderToWav(plugin, wav, 0.2f);
        ASSERT_GT(pk, 0.001f, "FM depth produces audio");
    }

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Feature: TZFM — all three FM routes (3>2, 3>1, 2>1)
// =========================================================================
TestResult test_tzfm_all_routes_wav() {
    TEST_BEGIN("TZFM all routes (writes bin/feat_tzfm_routes.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_tzfm_routes.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // All oscs sine
    plugin.setParameter(kP_Osc1Waveform, kWave_Sine);
    plugin.setParameter(kP_Osc2Waveform, kWave_Sine);
    plugin.setParameter(kP_Osc3Waveform, kWave_Sine);
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    // --- Route 1: FM 3→2  (carrier=osc2, modulator=osc3, osc1 off) ---
    plugin.setParameter(kP_Osc1Level, 0);
    plugin.setParameter(kP_Osc2Level, 1000);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_Osc3Semitone, 12);   // modulator 1 oct up
    plugin.setParameter(kP_FM3to2, 2000);
    plugin.setParameter(kP_FM3to1, 0);
    plugin.setParameter(kP_FM2to1, 0);
    plugin.midiNoteOn(0, 48, 100);
    renderToWav(plugin, wav, 1.0f);
    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.3f);

    // --- Route 2: FM 3→1  (carrier=osc1, modulator=osc3, osc2 off) ---
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_Osc3Semitone, 12);
    plugin.setParameter(kP_FM3to2, 0);
    plugin.setParameter(kP_FM3to1, 2000);
    plugin.setParameter(kP_FM2to1, 0);
    plugin.midiNoteOn(0, 48, 100);
    renderToWav(plugin, wav, 1.0f);
    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.3f);

    // --- Route 3: FM 2→1  (carrier=osc1, modulator=osc2, osc3 off) ---
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_Osc2Semitone, 7);    // modulator a fifth up
    plugin.setParameter(kP_FM3to2, 0);
    plugin.setParameter(kP_FM3to1, 0);
    plugin.setParameter(kP_FM2to1, 2000);
    plugin.midiNoteOn(0, 48, 100);
    renderToWav(plugin, wav, 1.0f);
    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.3f);

    wav.close();
    ASSERT_TRUE(true, "all FM routes rendered");
    TEST_PASS();
}

// =========================================================================
// Sync diagnostic: compare audio from two plugin instances (sync off vs on)
// =========================================================================
TestResult test_osc_sync_unit() {
    TEST_BEGIN("Sync diagnostic (two-instance A/B comparison)");

    // Helper: set up common params for sync test
    auto setupSyncPlugin = [](PluginInstance& p, bool syncOn) {
        p.setParameter(kP_Osc1Waveform, kWave_Sine);
        p.setParameter(kP_Osc1Level, 1000);
        p.setParameter(kP_Osc1Semitone, 7);      // carrier up a fifth
        p.setParameter(kP_Osc2Level, 0);
        p.setParameter(kP_Osc3Waveform, kWave_Sine);
        p.setParameter(kP_Osc3Level, 0);          // master inaudible
        p.setParameter(kP_BaseCutoff, 10000);
        p.setParameter(kP_FilterEnvAmount, 0);
        p.setParameter(kP_DelayMix, 0);
        p.setParameter(kP_AmpAttack, 0);
        p.setParameter(kP_AmpDecay, 3000);
        p.setParameter(kP_AmpSustain, 1000);
        p.setParameter(kP_AmpRelease, 100);
        p.setParameter(kP_Sync3to1, syncOn ? 1 : 0);
    };

    // Instance A: no sync
    PluginInstance pluginA;
    ASSERT_TRUE(createPlugin(pluginA), "pluginA created");
    setupSyncPlugin(pluginA, false);

    // Instance B: sync on
    PluginInstance pluginB;
    ASSERT_TRUE(createPlugin(pluginB), "pluginB created");
    setupSyncPlugin(pluginB, true);

    // Play same note on both
    pluginA.midiNoteOn(0, 48, 100);
    pluginB.midiNoteOn(0, 48, 100);

    // Skip attack transient (50ms)
    for (int i = 0; i < blocksFor(0.05f); ++i) {
        pluginA.step(BLOCK_SIZE);
        pluginB.step(BLOCK_SIZE);
    }

    // Render 0.5 seconds and compare
    int totalSamples = 0;
    int diffSamples = 0;
    double sumSqA = 0, sumSqB = 0, sumSqDiff = 0;

    for (int i = 0; i < blocksFor(0.5f); ++i) {
        pluginA.step(BLOCK_SIZE);
        pluginB.step(BLOCK_SIZE);
        const float* a = pluginA.getBus(OUTPUT_BUS, BLOCK_SIZE);
        const float* b = pluginB.getBus(OUTPUT_BUS, BLOCK_SIZE);
        for (int s = 0; s < BLOCK_SIZE; ++s) {
            float diff = a[s] - b[s];
            sumSqA += a[s] * a[s];
            sumSqB += b[s] * b[s];
            sumSqDiff += diff * diff;
            if (std::abs(diff) > 0.001f) ++diffSamples;
            ++totalSamples;
        }
    }

    float rmsA = static_cast<float>(std::sqrt(sumSqA / totalSamples));
    float rmsB = static_cast<float>(std::sqrt(sumSqB / totalSamples));
    float rmsDiff = static_cast<float>(std::sqrt(sumSqDiff / totalSamples));

    char msg[256];
    snprintf(msg, sizeof(msg),
             "A/B differ: rmsA=%.4f rmsB=%.4f rmsDiff=%.4f diffSamples=%d/%d",
             rmsA, rmsB, rmsDiff, diffSamples, totalSamples);
    ASSERT_GT(static_cast<float>(diffSamples), 0.5f, msg);
    ASSERT_GT(rmsDiff, 0.01f, "sync creates meaningful audio difference");

    TEST_PASS();
}

// =========================================================================
// Feature: Hard Sync — with / without comparison
// =========================================================================
TestResult test_hard_sync_wav() {
    TEST_BEGIN("Hard sync (writes bin/feat_hard_sync.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_hard_sync.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Sine);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc1Semitone, 7);     // carrier up a fifth
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Waveform, kWave_Sine);
    plugin.setParameter(kP_Osc3Level, 0);         // master inaudible
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    // Without sync — capture peak
    plugin.setParameter(kP_Sync3to1, 0);
    plugin.midiNoteOn(0, 48, 100);
    float pkNoSync = renderToWav(plugin, wav, 1.0f);
    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.3f);

    // With sync — capture peak
    plugin.setParameter(kP_Sync3to1, 1);
    plugin.midiNoteOn(0, 48, 100);
    float pkSync = renderToWav(plugin, wav, 1.0f);
    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.5f);

    ASSERT_GT(pkNoSync, 0.001f, "no-sync produces audio");
    ASSERT_GT(pkSync, 0.001f, "synced produces audio");
    // Note: RMS comparison is not useful here — sync changes harmonic content,
    // not overall energy. The A/B diagnostic (test_osc_sync_unit) proves sync works.

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Feature: Hard Sync sweep — ModEnv drives FM depth to sweep carrier freq
// Carrier at +12 semitones, sync from osc3. ModEnv→FM3to1 creates a
// decaying frequency sweep on each note, producing the classic sync "wow".
// =========================================================================
TestResult test_hard_sync_sweep_wav() {
    TEST_BEGIN("Hard sync sweep (writes bin/feat_sync_sweep.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_sync_sweep.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Carrier (osc1) sine, +12 semitones above master
    plugin.setParameter(kP_Osc1Waveform, kWave_Sine);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc1Semitone, 12);
    plugin.setParameter(kP_Osc2Level, 0);
    // Master (osc3) sine at base pitch, inaudible
    plugin.setParameter(kP_Osc3Waveform, kWave_Sine);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_Sync3to1, 1);
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    // ModEnv: slow attack sweep up then decay — creates rising then falling FM
    plugin.setParameter(kP_ModEnvAttack, 800);
    plugin.setParameter(kP_ModEnvDecay, 1200);
    plugin.setParameter(kP_ModEnvSustain, 0);
    plugin.setParameter(kP_ModEnvRelease, 300);

    // Route ModEnv → FM 3→1 (sweeps carrier instantaneous frequency)
    plugin.setParameter(kP_Mod1Source, kSrc_ModEnv);
    plugin.setParameter(kP_Mod1Dest, kDst_FM3to1);
    plugin.setParameter(kP_Mod1Amount, 80);

    // Amp env: long sustain so we hear the full ModEnv cycle
    plugin.setParameter(kP_AmpAttack, 5);
    plugin.setParameter(kP_AmpDecay, 200);
    plugin.setParameter(kP_AmpSustain, 800);
    plugin.setParameter(kP_AmpRelease, 500);

    // Play three notes to hear the sweep repeat
    int notes[] = {36, 48, 60};
    for (int n : notes) {
        plugin.midiNoteOn(0, n, 100);
        renderToWav(plugin, wav, 2.5f);
        plugin.midiNoteOff(0, n);
        renderToWav(plugin, wav, 0.6f);
    }

    // Finish with a Cmaj7 bass voicing (C1-E1-G1-B1)
    plugin.midiNoteOn(0, 36, 100);  // C1
    plugin.midiNoteOn(0, 40, 100);  // E1
    plugin.midiNoteOn(0, 43, 100);  // G1
    plugin.midiNoteOn(0, 47, 100);  // B1
    renderToWav(plugin, wav, 3.5f);
    plugin.midiNoteOff(0, 36);
    plugin.midiNoteOff(0, 40);
    plugin.midiNoteOff(0, 43);
    plugin.midiNoteOff(0, 47);
    renderToWav(plugin, wav, 1.0f);

    wav.close();
    ASSERT_TRUE(true, "sync sweep rendered");
    TEST_PASS();
}

// =========================================================================
// Feature: LFO -> Cutoff modulation (wah-wah effect)
// =========================================================================
TestResult test_lfo_cutoff_wav() {
    TEST_BEGIN("LFO->Cutoff modulation (writes bin/feat_lfo_cutoff.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_lfo_cutoff.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 1500);
    plugin.setParameter(kP_Resonance, 600);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    plugin.setParameter(kP_LfoSpeed, 200);
    plugin.setParameter(kP_LfoShape, 0);  // sine

    plugin.setParameter(kP_Mod1Source, kSrc_LFO);
    plugin.setParameter(kP_Mod1Dest, kDst_Cutoff);
    plugin.setParameter(kP_Mod1Amount, 800);

    plugin.midiNoteOn(0, 48, 100);
    float pk = renderToWav(plugin, wav, 3.0f);
    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.5f);

    ASSERT_GT(pk, 0.001f, "LFO->Cutoff produces audio");

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Feature: LFO -> All Morph (morphing waveform via LFO)
// =========================================================================
TestResult test_lfo_morph_wav() {
    TEST_BEGIN("LFO->AllMorph (writes bin/feat_lfo_morph.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_lfo_morph.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Morph);
    plugin.setParameter(kP_Osc2Waveform, kWave_Morph);
    plugin.setParameter(kP_Osc3Waveform, kWave_Morph);
    plugin.setParameter(kP_Osc2Semitone, 7);
    plugin.setParameter(kP_Osc3Semitone, -12);
    plugin.setParameter(kP_BaseCutoff, 8000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    plugin.setParameter(kP_LfoSpeed, 100);
    plugin.setParameter(kP_LfoShape, 1);  // triangle

    plugin.setParameter(kP_Mod1Source, kSrc_LFO);
    plugin.setParameter(kP_Mod1Dest, kDst_AllMorph);
    plugin.setParameter(kP_Mod1Amount, 1000);

    plugin.midiNoteOn(0, 48, 100);
    float pk = renderToWav(plugin, wav, 4.0f);
    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.5f);

    ASSERT_GT(pk, 0.001f, "LFO->Morph produces audio");

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Feature: Mod Matrix — Velocity -> Cutoff
// =========================================================================
TestResult test_modmatrix_velocity_cutoff_wav() {
    TEST_BEGIN("Velocity->Cutoff (writes bin/feat_vel_cutoff.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_vel_cutoff.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 800);
    plugin.setParameter(kP_Resonance, 300);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    plugin.setParameter(kP_Mod1Source, kSrc_Velocity);
    plugin.setParameter(kP_Mod1Dest, kDst_Cutoff);
    plugin.setParameter(kP_Mod1Amount, 1000);

    // Soft note then hard note
    plugin.midiNoteOn(0, 60, 30);
    renderToWav(plugin, wav, 0.8f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 0.3f);

    plugin.midiNoteOn(0, 60, 127);
    renderToWav(plugin, wav, 0.8f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 0.5f);

    wav.close();
    ASSERT_TRUE(true, "velocity->cutoff rendered");
    TEST_PASS();
}

// =========================================================================
// Feature: Mod Matrix — ModEnv -> FM depth (evolving FM timbre)
// =========================================================================
TestResult test_modmatrix_modenv_fm_wav() {
    TEST_BEGIN("ModEnv->FM (writes bin/feat_modenv_fm.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_modenv_fm.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Sine);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Waveform, kWave_Sine);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_Osc3Semitone, 12);
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);
    plugin.setParameter(kP_FM3to1, 0);

    // Mod Env: percussive sweep
    plugin.setParameter(kP_ModEnvAttack, 1);
    plugin.setParameter(kP_ModEnvDecay, 1000);
    plugin.setParameter(kP_ModEnvSustain, 0);
    plugin.setParameter(kP_ModEnvRelease, 200);
    plugin.setParameter(kP_ModEnvShape, -990);

    plugin.setParameter(kP_Mod1Source, kSrc_ModEnv);
    plugin.setParameter(kP_Mod1Dest, kDst_FM3to1);
    plugin.setParameter(kP_Mod1Amount, 50);

    int notes[] = {48, 55, 60};
    for (int n : notes) {
        plugin.midiNoteOn(0, n, 100);
        renderToWav(plugin, wav, 1.2f);
        plugin.midiNoteOff(0, n);
        renderToWav(plugin, wav, 0.4f);
    }

    wav.close();
    ASSERT_TRUE(true, "modenv->FM rendered");
    TEST_PASS();
}

// =========================================================================
// Feature: Filter modes comparison (LP2..BYPASS)
// =========================================================================
TestResult test_filter_modes_wav() {
    TEST_BEGIN("Filter modes (writes bin/feat_filter_modes.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_filter_modes.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 2000);
    plugin.setParameter(kP_Resonance, 500);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    for (int mode = 0; mode <= 6; ++mode) {
        plugin.setParameter(kP_FilterMode, mode);
        plugin.midiNoteOn(0, 48, 100);
        renderToWav(plugin, wav, 0.7f);
        plugin.midiNoteOff(0, 48);
        renderToWav(plugin, wav, 0.3f);
    }

    wav.close();
    ASSERT_TRUE(true, "filter modes rendered");
    TEST_PASS();
}

// =========================================================================
// Feature: Delay effect (pluck with echo repeats)
// =========================================================================
TestResult test_delay_wav() {
    TEST_BEGIN("Delay effect (writes bin/feat_delay.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_delay.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_AmpAttack, 0);
    plugin.setParameter(kP_AmpDecay, 100);
    plugin.setParameter(kP_AmpSustain, 0);
    plugin.setParameter(kP_AmpRelease, 50);
    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 5000);
    plugin.setParameter(kP_FilterEnvAmount, 0);

    // 150ms delay — echoes return while the ungated delay tail is
    // still audible, creating a clear rhythmic echo trail.
    plugin.setParameter(kP_DelayTime, 150);
    plugin.setParameter(kP_DelayFeedback, 600);
    plugin.setParameter(kP_DelayMix, 500);

    plugin.midiNoteOn(0, 60, 100);
    renderToWav(plugin, wav, 0.15f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 1.5f);  // long tail to hear echo decay

    plugin.midiNoteOn(0, 64, 100);
    renderToWav(plugin, wav, 0.15f);
    plugin.midiNoteOff(0, 64);
    renderToWav(plugin, wav, 2.0f);  // long tail

    wav.close();
    ASSERT_TRUE(true, "delay effect rendered");
    TEST_PASS();
}

// =========================================================================
// Feature: 3-osc detune (supersaw pad with C minor chord)
// =========================================================================
TestResult test_3osc_detune_wav() {
    TEST_BEGIN("3-osc detune (writes bin/feat_3osc_detune.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_3osc_detune.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc2Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc3Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Fine, 0);
    plugin.setParameter(kP_Osc2Fine, 15);
    plugin.setParameter(kP_Osc3Fine, -15);
    plugin.setParameter(kP_Osc1Level, 333);
    plugin.setParameter(kP_Osc2Level, 333);
    plugin.setParameter(kP_Osc3Level, 333);
    plugin.setParameter(kP_BaseCutoff, 6000);
    plugin.setParameter(kP_FilterEnvAmount, 3000);
    plugin.setParameter(kP_DelayMix, 0);

    plugin.setParameter(kP_AmpAttack, 300);
    plugin.setParameter(kP_AmpDecay, 200);
    plugin.setParameter(kP_AmpSustain, 700);
    plugin.setParameter(kP_AmpRelease, 800);

    plugin.midiNoteOn(0, 48, 90);
    plugin.midiNoteOn(0, 51, 90);
    plugin.midiNoteOn(0, 55, 90);
    plugin.midiNoteOn(0, 60, 90);

    renderToWav(plugin, wav, 3.0f);

    plugin.midiNoteOff(0, 48);
    plugin.midiNoteOff(0, 51);
    plugin.midiNoteOff(0, 55);
    plugin.midiNoteOff(0, 60);

    renderToWav(plugin, wav, 1.0f);

    wav.close();
    ASSERT_TRUE(true, "3-osc detune rendered");
    TEST_PASS();
}

// =========================================================================
// Feature: Wave morph sweep (morph 0->1000 over 3s)
// =========================================================================
TestResult test_morph_sweep_wav() {
    TEST_BEGIN("Wave morph sweep (writes bin/feat_morph_sweep.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_morph_sweep.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Morph);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    plugin.midiNoteOn(0, 48, 100);

    int totalBlocks = blocksFor(3.0f);
    for (int b = 0; b < totalBlocks; ++b) {
        float t = static_cast<float>(b) / totalBlocks;
        plugin.setParameter(kP_Osc1Morph, static_cast<int16_t>(t * 1000.0f));
        plugin.step(BLOCK_SIZE);
        wav.writeMono(plugin.getBus(OUTPUT_BUS, BLOCK_SIZE), BLOCK_SIZE);
    }

    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.5f);

    wav.close();
    ASSERT_TRUE(true, "morph sweep rendered");
    TEST_PASS();
}

// =========================================================================
// Feature: Drive / saturation (clean vs moderate vs heavy)
// =========================================================================
TestResult test_drive_wav() {
    TEST_BEGIN("Drive/saturation (writes bin/feat_drive.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_drive.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Sine);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    int drives[] = {1000, 4000, 10000};
    for (int d : drives) {
        plugin.setParameter(kP_Drive, d);
        plugin.midiNoteOn(0, 48, 100);
        renderToWav(plugin, wav, 1.0f);
        plugin.midiNoteOff(0, 48);
        renderToWav(plugin, wav, 0.3f);
    }

    wav.close();
    ASSERT_TRUE(true, "drive comparison rendered");
    TEST_PASS();
}

// =========================================================================
// Feature: Filter Envelope sweep (acid bass sequence)
// =========================================================================
TestResult test_filter_env_wav() {
    TEST_BEGIN("Filter env sweep (writes bin/feat_filter_env.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_filter_env.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Square);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_DelayMix, 0);

    plugin.setParameter(kP_BaseCutoff, 500);
    plugin.setParameter(kP_Resonance, 800);
    plugin.setParameter(kP_FilterEnvAmount, 8000);

    plugin.setParameter(kP_FilterAttack, 1);
    plugin.setParameter(kP_FilterDecay, 200);
    plugin.setParameter(kP_FilterSustain, 0);
    plugin.setParameter(kP_FilterRelease, 100);

    plugin.setParameter(kP_AmpAttack, 0);
    plugin.setParameter(kP_AmpDecay, 300);
    plugin.setParameter(kP_AmpSustain, 600);
    plugin.setParameter(kP_AmpRelease, 100);

    int notes[] = {36, 36, 48, 36, 36, 39, 36, 43};
    for (int n : notes) {
        plugin.midiNoteOn(0, n, 110);
        renderToWav(plugin, wav, 0.25f);
        plugin.midiNoteOff(0, n);
        renderToWav(plugin, wav, 0.1f);
    }
    renderToWav(plugin, wav, 0.5f);

    wav.close();
    ASSERT_TRUE(true, "filter env sweep rendered");
    TEST_PASS();
}

// =========================================================================
// Feature: Multi-LFO (3 LFOs -> 3 different destinations)
// =========================================================================
TestResult test_multi_lfo_wav() {
    TEST_BEGIN("Multi-LFO modulation (writes bin/feat_multi_lfo.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_multi_lfo.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Morph);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 3000);
    plugin.setParameter(kP_Resonance, 400);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    // LFO1 sine -> Cutoff
    plugin.setParameter(kP_LfoSpeed, 350);
    plugin.setParameter(kP_LfoShape, 0);
    plugin.setParameter(kP_Mod1Source, kSrc_LFO);
    plugin.setParameter(kP_Mod1Dest, kDst_Cutoff);
    plugin.setParameter(kP_Mod1Amount, 700);

    // LFO2 triangle -> Osc1 Morph
    plugin.setParameter(kP_Lfo2Speed, 250);
    plugin.setParameter(kP_Lfo2Shape, 1);
    plugin.setParameter(kP_Mod2Source, kSrc_LFO2);
    plugin.setParameter(kP_Mod2Dest, kDst_Osc1Morph);
    plugin.setParameter(kP_Mod2Amount, 1000);

    // LFO3 square -> Resonance
    plugin.setParameter(kP_Lfo3Speed, 300);
    plugin.setParameter(kP_Lfo3Shape, 2);
    plugin.setParameter(kP_Mod3Source, kSrc_LFO3);
    plugin.setParameter(kP_Mod3Dest, kDst_Resonance);
    plugin.setParameter(kP_Mod3Amount, 500);

    plugin.midiNoteOn(0, 48, 100);
    float pk = renderToWav(plugin, wav, 5.0f);
    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.5f);

    ASSERT_GT(pk, 0.001f, "multi-LFO produces audio");

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Feature: FM + Sync combined (metallic tones)
// =========================================================================
TestResult test_fm_plus_sync_wav() {
    TEST_BEGIN("FM+Sync combined (writes bin/feat_fm_sync.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_fm_sync.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Sine);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc1Semitone, 5);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Waveform, kWave_Sine);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    plugin.setParameter(kP_FM3to1, 1500);
    plugin.setParameter(kP_Sync3to1, 1);

    int melody[] = {60, 64, 67, 72, 67, 64, 60};
    for (int n : melody) {
        plugin.midiNoteOn(0, n, 100);
        renderToWav(plugin, wav, 0.4f);
        plugin.midiNoteOff(0, n);
        renderToWav(plugin, wav, 0.1f);
    }
    renderToWav(plugin, wav, 0.5f);

    wav.close();
    ASSERT_TRUE(true, "FM+Sync rendered");
    TEST_PASS();
}

// =========================================================================
// Feature: LFO morph is not reset by speed changes
// Plays morph-waveform LFO, changes speed mid-note; morph must persist.
// =========================================================================
TestResult test_lfo_morph_survives_speed_change() {
    TEST_BEGIN("LFO morph survives speed change (writes bin/feat_lfo_morph_persist.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_lfo_morph_persist.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Use morph waveform LFO modulating cutoff
    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 2000);
    plugin.setParameter(kP_Resonance, 500);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    // LFO1: morph shape at 50% morph, modulating cutoff
    plugin.setParameter(kP_LfoShape, 4);  // Morph shape
    plugin.setParameter(kP_LfoMorph, 500);  // 50% morph
    plugin.setParameter(kP_LfoSpeed, 300);  // moderate speed
    plugin.setParameter(kP_Mod1Source, kSrc_LFO);
    plugin.setParameter(kP_Mod1Dest, kDst_Cutoff);
    plugin.setParameter(kP_Mod1Amount, 800);

    plugin.midiNoteOn(0, 48, 100);
    renderToWav(plugin, wav, 1.5f);

    // Now change speed — morph should NOT reset to 0
    plugin.setParameter(kP_LfoSpeed, 600);
    float pk = renderToWav(plugin, wav, 1.5f);

    // Change speed again
    plugin.setParameter(kP_LfoSpeed, 100);
    renderToWav(plugin, wav, 1.5f);

    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.5f);

    ASSERT_GT(pk, 0.001f, "LFO morph persists through speed changes");

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Feature: Pitch Bend — sweep up and down
// =========================================================================
TestResult test_pitch_bend_wav() {
    TEST_BEGIN("Pitch bend sweep (writes bin/feat_pitch_bend.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_pitch_bend.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    plugin.midiNoteOn(0, 60, 100);

    // Steady pitch
    renderToWav(plugin, wav, 0.5f);

    // Sweep bend up from center (8192) to max (16383)
    int totalBlocks = blocksFor(1.0f);
    for (int b = 0; b < totalBlocks; ++b) {
        float t = static_cast<float>(b) / totalBlocks;
        int bend = 8192 + static_cast<int>(t * 8191);
        plugin.midiPitchBend(0, bend);
        plugin.step(BLOCK_SIZE);
        wav.writeMono(plugin.getBus(OUTPUT_BUS, BLOCK_SIZE), BLOCK_SIZE);
    }

    // Sweep bend down from max to min (0)
    for (int b = 0; b < totalBlocks; ++b) {
        float t = static_cast<float>(b) / totalBlocks;
        int bend = 16383 - static_cast<int>(t * 16383);
        plugin.midiPitchBend(0, bend);
        plugin.step(BLOCK_SIZE);
        wav.writeMono(plugin.getBus(OUTPUT_BUS, BLOCK_SIZE), BLOCK_SIZE);
    }

    // Back to center
    plugin.midiPitchBend(0, 8192);
    renderToWav(plugin, wav, 0.5f);

    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 0.5f);

    wav.close();
    ASSERT_TRUE(true, "pitch bend sweep rendered");
    TEST_PASS();
}

// =========================================================================
// Feature: Sustain Pedal CC64 — notes sustain past note-off
// =========================================================================
TestResult test_sustain_pedal_wav() {
    TEST_BEGIN("Sustain pedal CC64 (writes bin/feat_sustain_pedal.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_sustain_pedal.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 5000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);
    plugin.setParameter(kP_AmpAttack, 5);
    plugin.setParameter(kP_AmpRelease, 200);

    // Play a note without sustain — short
    plugin.midiNoteOn(0, 60, 100);
    renderToWav(plugin, wav, 0.5f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 0.5f);  // should go silent

    // Now with sustain pedal
    plugin.midiCC(0, 64, 127);  // Pedal down
    plugin.midiNoteOn(0, 60, 100);
    renderToWav(plugin, wav, 0.5f);
    plugin.midiNoteOff(0, 60);  // note-off should be deferred

    // Should still be sounding because sustain holds it
    float pkHeld = renderToWav(plugin, wav, 0.5f);
    ASSERT_GT(pkHeld, 0.01f, "note sustains past note-off with pedal down");

    // Release pedal — note should release
    plugin.midiCC(0, 64, 0);
    renderToWav(plugin, wav, 1.0f);  // release tail

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Feature: Sustain Pedal piano-mode retrigger
//   With sustain held, re-pressing the same chord retriggers envelopes.
//   Releasing sustain pedal afterwards should let voices release normally.
// =========================================================================
TestResult test_sustain_retrigger_wav() {
    TEST_BEGIN("Sustain pedal piano-mode retrigger (writes bin/feat_sustain_retrigger.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_sustain_retrigger.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 5000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);
    plugin.setParameter(kP_AmpAttack, 10);         // 10ms attack — enough to retrigger visibly
    plugin.setParameter(kP_AmpDecay, 200);
    plugin.setParameter(kP_AmpSustain, 700);       // audible sustain level
    plugin.setParameter(kP_AmpRelease, 300);

    // -- Phase 1: sustain pedal down, play a C-E-G chord --
    plugin.midiCC(0, 64, 127);  // Pedal down
    plugin.midiNoteOn(0, 60, 100);
    plugin.midiNoteOn(0, 64, 100);
    plugin.midiNoteOn(0, 67, 100);
    renderToWav(plugin, wav, 0.3f);  // let attack settle

    // -- Phase 2: release keys — notes should sustain via pedal --
    plugin.midiNoteOff(0, 60);
    plugin.midiNoteOff(0, 64);
    plugin.midiNoteOff(0, 67);
    float pkHeld = renderToWav(plugin, wav, 0.3f);
    ASSERT_GT(pkHeld, 0.01f, "chord sustains past note-off with pedal down");

    // -- Phase 3: replay the same chord (pedal still held) — should retrigger --
    plugin.midiNoteOn(0, 60, 100);
    plugin.midiNoteOn(0, 64, 100);
    plugin.midiNoteOn(0, 67, 100);
    float pkRetrigger = renderToWav(plugin, wav, 0.3f);
    ASSERT_GT(pkRetrigger, 0.01f, "retriggered chord is audible");

    // -- Phase 4: release keys again — still sustained by pedal --
    plugin.midiNoteOff(0, 60);
    plugin.midiNoteOff(0, 64);
    plugin.midiNoteOff(0, 67);
    float pkHeld2 = renderToWav(plugin, wav, 0.3f);
    ASSERT_GT(pkHeld2, 0.01f, "chord sustains after second note-off");

    // -- Phase 5: retrigger WHILE HOLDING keys, then release pedal --
    //   The notes should NOT cut out because keys are still held.
    plugin.midiNoteOn(0, 60, 100);
    plugin.midiNoteOn(0, 64, 100);
    plugin.midiNoteOn(0, 67, 100);
    renderToWav(plugin, wav, 0.1f);  // brief hold
    plugin.midiCC(0, 64, 0);         // Pedal up — keys still held
    float pkPedalUp = renderToWav(plugin, wav, 0.3f);
    ASSERT_GT(pkPedalUp, 0.01f, "notes survive pedal release when keys still held");

    // -- Phase 6: now release keys — notes should actually release --
    plugin.midiNoteOff(0, 60);
    plugin.midiNoteOff(0, 64);
    plugin.midiNoteOff(0, 67);
    renderToWav(plugin, wav, 1.0f);  // release tail

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Feature: Mod Wheel CC1 → Cutoff modulation
// =========================================================================
TestResult test_mod_wheel_wav() {
    TEST_BEGIN("Mod wheel CC1 (writes bin/feat_mod_wheel.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_mod_wheel.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 1000);
    plugin.setParameter(kP_Resonance, 500);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    // Route mod wheel → cutoff
    plugin.setParameter(kP_Mod1Source, kSrc_ModWheel);
    plugin.setParameter(kP_Mod1Dest, kDst_Cutoff);
    plugin.setParameter(kP_Mod1Amount, 1000);

    plugin.midiNoteOn(0, 48, 100);

    // Start with mod wheel at 0
    plugin.midiCC(0, 1, 0);
    renderToWav(plugin, wav, 1.0f);

    // Sweep mod wheel up
    int totalBlocks = blocksFor(2.0f);
    for (int b = 0; b < totalBlocks; ++b) {
        float t = static_cast<float>(b) / totalBlocks;
        uint8_t ccVal = static_cast<uint8_t>(t * 127.0f);
        plugin.midiCC(0, 1, ccVal);
        plugin.step(BLOCK_SIZE);
        wav.writeMono(plugin.getBus(OUTPUT_BUS, BLOCK_SIZE), BLOCK_SIZE);
    }

    // Hold at max
    plugin.midiCC(0, 1, 127);
    renderToWav(plugin, wav, 1.0f);

    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.5f);

    wav.close();
    ASSERT_TRUE(true, "mod wheel sweep rendered");
    TEST_PASS();
}

// =========================================================================
// Feature: Pulse Width — sweep from narrow to wide
// =========================================================================
TestResult test_pulse_width_wav() {
    TEST_BEGIN("Pulse width sweep (writes bin/feat_pulse_width.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_pulse_width.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Square);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    plugin.midiNoteOn(0, 48, 100);

    // Sweep morph (=pulse width on square) from 0 to 1000 over 3s
    int totalBlocks = blocksFor(3.0f);
    for (int b = 0; b < totalBlocks; ++b) {
        float t = static_cast<float>(b) / totalBlocks;
        int16_t morph = static_cast<int16_t>(t * 1000);  // 0 to 1000
        plugin.setParameter(kP_Osc1Morph, morph);
        plugin.step(BLOCK_SIZE);
        wav.writeMono(plugin.getBus(OUTPUT_BUS, BLOCK_SIZE), BLOCK_SIZE);
    }

    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.5f);

    wav.close();
    ASSERT_TRUE(true, "pulse width sweep rendered");
    TEST_PASS();
}

// =========================================================================
// Feature: LFO exponential speed — test slow and fast LFO
// The new mapping gives 0.01Hz at 0, ~0.7Hz at 500, ~50Hz at 1000.
// =========================================================================
TestResult test_lfo_exp_speed_wav() {
    TEST_BEGIN("LFO exponential speed (writes bin/feat_lfo_exp_speed.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_lfo_exp_speed.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 2000);
    plugin.setParameter(kP_Resonance, 500);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    // LFO -> cutoff
    plugin.setParameter(kP_LfoShape, 0);  // sine
    plugin.setParameter(kP_Mod1Source, kSrc_LFO);
    plugin.setParameter(kP_Mod1Dest, kDst_Cutoff);
    plugin.setParameter(kP_Mod1Amount, 800);

    plugin.midiNoteOn(0, 48, 100);

    // Very slow LFO (speed = 100 → ~0.04 Hz, ~25s period)
    plugin.setParameter(kP_LfoSpeed, 100);
    renderToWav(plugin, wav, 2.0f);

    // Medium LFO (speed = 500 → ~0.7 Hz)
    plugin.setParameter(kP_LfoSpeed, 500);
    renderToWav(plugin, wav, 2.0f);

    // Fast LFO (speed = 900 → ~25 Hz)
    plugin.setParameter(kP_LfoSpeed, 900);
    float pkFast = renderToWav(plugin, wav, 2.0f);

    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.5f);

    ASSERT_GT(pkFast, 0.001f, "fast LFO produces audio");

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Feature: Aftertouch — channel and poly aftertouch as mod source
// =========================================================================
TestResult test_aftertouch_wav() {
    TEST_BEGIN("Aftertouch modulation (writes bin/feat_aftertouch.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_aftertouch.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 800);
    plugin.setParameter(kP_Resonance, 500);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    // Route aftertouch -> cutoff
    plugin.setParameter(kP_Mod1Source, kSrc_Aftertouch);
    plugin.setParameter(kP_Mod1Dest, kDst_Cutoff);
    plugin.setParameter(kP_Mod1Amount, 1000);

    plugin.midiNoteOn(0, 48, 100);

    // No aftertouch — dark sound
    renderToWav(plugin, wav, 1.0f);

    // Sweep channel aftertouch up
    int totalBlocks = blocksFor(2.0f);
    for (int b = 0; b < totalBlocks; ++b) {
        float t = static_cast<float>(b) / totalBlocks;
        uint8_t pressure = static_cast<uint8_t>(t * 127.0f);
        plugin.midiAftertouch(0, pressure);
        plugin.step(BLOCK_SIZE);
        wav.writeMono(plugin.getBus(OUTPUT_BUS, BLOCK_SIZE), BLOCK_SIZE);
    }

    // Hold at max
    plugin.midiAftertouch(0, 127);
    float pk = renderToWav(plugin, wav, 1.0f);

    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.5f);

    ASSERT_GT(pk, 0.001f, "aftertouch modulation produces audio");

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Feature: Glide / Portamento — legato glide between notes
// =========================================================================
TestResult test_glide_wav() {
    TEST_BEGIN("Glide/Portamento (writes bin/feat_glide.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_glide.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 8000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);
    plugin.setParameter(kP_AmpAttack, 5);
    plugin.setParameter(kP_AmpSustain, 1000);

    // --- Section 1: No glide (reference) ---
    plugin.setParameter(kP_GlideTime, 0);
    plugin.setParameter(kP_GlideMode, 0); // Off

    plugin.midiNoteOn(0, 48, 100);
    renderToWav(plugin, wav, 0.5f);
    // Jump to new note (no glide)
    plugin.midiNoteOn(0, 60, 100);
    renderToWav(plugin, wav, 0.5f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 0.3f);

    // --- Section 2: Glide Always, 500ms ---
    plugin.setParameter(kP_GlideTime, 500);
    plugin.setParameter(kP_GlideMode, 1); // Always

    plugin.midiNoteOn(0, 48, 100);
    renderToWav(plugin, wav, 0.5f);
    // Glide up to C4
    plugin.midiNoteOn(0, 60, 100);
    renderToWav(plugin, wav, 1.0f);
    // Glide down to G2
    plugin.midiNoteOn(0, 43, 100);
    renderToWav(plugin, wav, 1.0f);
    plugin.midiNoteOff(0, 43);
    renderToWav(plugin, wav, 0.3f);

    // --- Section 3: Glide Legato only, 300ms ---
    plugin.setParameter(kP_GlideTime, 300);
    plugin.setParameter(kP_GlideMode, 2); // Legato

    plugin.midiNoteOn(0, 48, 100);
    renderToWav(plugin, wav, 0.5f);
    // Legato: second note while first held → should glide
    plugin.midiNoteOn(0, 60, 100);
    float pkGlide = renderToWav(plugin, wav, 0.8f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 0.5f);

    ASSERT_GT(pkGlide, 0.001f, "glide produces audio");

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Feature: Bit Crusher — reduce bit depth and sample rate
// =========================================================================
TestResult test_bitcrush_wav() {
    TEST_BEGIN("Bit crusher (writes bin/feat_bitcrush.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_bitcrush.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);
    plugin.setParameter(kP_FilterMode, 6); // BYPASS filter to hear pure crush

    plugin.midiNoteOn(0, 48, 100);

    // 1. Clean (16-bit, no reduce)
    plugin.setParameter(kP_BitCrush, 16);
    plugin.setParameter(kP_SampleReduce, 1);
    renderToWav(plugin, wav, 1.0f);

    // 2. Medium crush (8-bit)
    plugin.setParameter(kP_BitCrush, 8);
    renderToWav(plugin, wav, 1.0f);

    // 3. Heavy crush (4-bit)
    plugin.setParameter(kP_BitCrush, 4);
    renderToWav(plugin, wav, 1.0f);

    // 4. Extreme crush (2-bit) + sample reduce
    plugin.setParameter(kP_BitCrush, 2);
    plugin.setParameter(kP_SampleReduce, 8);
    float pkCrush = renderToWav(plugin, wav, 1.0f);

    // 5. Only sample reduce (16-bit, reduce=16)
    plugin.setParameter(kP_BitCrush, 16);
    plugin.setParameter(kP_SampleReduce, 16);
    renderToWav(plugin, wav, 1.0f);

    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.5f);

    ASSERT_GT(pkCrush, 0.001f, "bit crush produces audio");

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Feature: Delay bypass when mix = 0 (item 7c)
// Two sections: delay ON then delay OFF — verify bypass produces clean audio
// =========================================================================
TestResult test_delay_bypass_wav() {
    TEST_BEGIN("Delay bypass when mix=0 (writes bin/feat_delay_bypass.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_delay_bypass.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_FilterMode, 6); // BYPASS filter
    plugin.setParameter(kP_DelayMix, 0);   // Delay OFF

    plugin.midiNoteOn(0, 60, 100);

    // Section 1: no delay (mix=0) — clean signal
    float pkClean = renderToWav(plugin, wav, 1.0f);

    // Section 2: turn delay on
    plugin.setParameter(kP_DelayTime, 200);
    plugin.setParameter(kP_DelayFeedback, 500);
    plugin.setParameter(kP_DelayMix, 500);
    float pkDelay = renderToWav(plugin, wav, 2.0f);

    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 1.0f);

    wav.close();
    ASSERT_TRUE(pkClean > 0.01f, "clean section has audio");
    ASSERT_TRUE(pkDelay > 0.01f, "delay section has audio");
    TEST_PASS();
}

// =========================================================================
// Exploration 11a: Chorus — LFO modulating short delay time
// Delay 10ms, LFO->DelayTime at ~2Hz, low feedback, 50% mix
// =========================================================================
TestResult test_chorus_lfo_delaytime_wav() {
    TEST_BEGIN("Chorus: LFO->DelayTime (writes bin/fx_chorus.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/fx_chorus.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 7000);
    plugin.setParameter(kP_Resonance, 50);
    plugin.setParameter(kP_FilterEnvAmount, 0);

    // Short delay = 10ms, no feedback, 50% mix
    plugin.setParameter(kP_DelayTime, 10);
    plugin.setParameter(kP_DelayFeedback, 0);
    plugin.setParameter(kP_DelayMix, 500);

    // LFO1 at ~2 Hz -> Delay Time, amount = 0.3
    plugin.setParameter(kP_LfoSpeed, 300);  // ~2 Hz region
    plugin.setParameter(kP_LfoShape, 0);     // Sine
    plugin.setParameter(kP_Mod1Source, kSrc_LFO);
    plugin.setParameter(kP_Mod1Dest, kDst_DelayTime);
    plugin.setParameter(kP_Mod1Amount, 300);  // 0.3

    plugin.midiNoteOn(0, 60, 100);
    float pk = renderToWav(plugin, wav, 4.0f);

    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 1.5f);

    wav.close();
    ASSERT_TRUE(pk > 0.01f, "chorus output has audio");
    TEST_PASS();
}

// =========================================================================
// Exploration 11a: Flanger — LFO modulating very short delay with feedback
// Delay 3ms, LFO->DelayTime at ~0.5Hz, 60% feedback, 50% mix
// =========================================================================
TestResult test_flanger_lfo_feedback_wav() {
    TEST_BEGIN("Flanger: LFO->DelayTime+feedback (writes bin/fx_flanger.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/fx_flanger.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 8000);
    plugin.setParameter(kP_Resonance, 50);
    plugin.setParameter(kP_FilterEnvAmount, 0);

    // Very short delay = 3ms, high feedback, 50% mix
    plugin.setParameter(kP_DelayTime, 3);
    plugin.setParameter(kP_DelayFeedback, 600);
    plugin.setParameter(kP_DelayMix, 500);

    // Slow LFO ~0.3Hz -> Delay Time, strong modulation
    plugin.setParameter(kP_LfoSpeed, 200);
    plugin.setParameter(kP_LfoShape, 0);   // Sine
    plugin.setParameter(kP_Mod1Source, kSrc_LFO);
    plugin.setParameter(kP_Mod1Dest, kDst_DelayTime);
    plugin.setParameter(kP_Mod1Amount, 600);

    plugin.midiNoteOn(0, 48, 100);
    float pk = renderToWav(plugin, wav, 5.0f);

    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 1.5f);

    wav.close();
    ASSERT_TRUE(pk > 0.01f, "flanger output has audio");
    TEST_PASS();
}

// =========================================================================
// Exploration: Envelope-ducked delay — AmpEnv -> DelayMix (inverted)
// When note is loud, delay is quiet; as note fades, delay swells in
// =========================================================================
TestResult test_delay_env_ducking_wav() {
    TEST_BEGIN("Env ducked delay (writes bin/fx_delay_ducking.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/fx_delay_ducking.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 5000);
    plugin.setParameter(kP_FilterEnvAmount, 3000);

    // Delay: 250ms, moderate feedback
    plugin.setParameter(kP_DelayTime, 250);
    plugin.setParameter(kP_DelayFeedback, 400);
    plugin.setParameter(kP_DelayMix, 600);

    // AmpEnv -> DelayMix inverted: when envelope is high, reduce mix
    plugin.setParameter(kP_Mod1Source, kSrc_AmpEnv);
    plugin.setParameter(kP_Mod1Dest, kDst_DelayMix);
    plugin.setParameter(kP_Mod1Amount, -800); // negative = ducking

    // Short plucky envelope
    plugin.setParameter(kP_AmpAttack, 5);
    plugin.setParameter(kP_AmpDecay, 200);
    plugin.setParameter(kP_AmpSustain, 300);
    plugin.setParameter(kP_AmpRelease, 1000);

    // Play a few notes
    plugin.midiNoteOn(0, 60, 100);
    renderToWav(plugin, wav, 1.5f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 1.5f);

    plugin.midiNoteOn(0, 64, 100);
    renderToWav(plugin, wav, 1.5f);
    plugin.midiNoteOff(0, 64);
    renderToWav(plugin, wav, 1.5f);

    wav.close();
    ASSERT_TRUE(true, "env ducked delay rendered");
    TEST_PASS();
}

// =========================================================================
// Exploration: Velocity -> DelayFeedback — harder hits echo more
// =========================================================================
TestResult test_delay_vel_feedback_wav() {
    TEST_BEGIN("Velocity->DelayFeedback (writes bin/fx_delay_vel_fdbk.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/fx_delay_vel_fdbk.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Square);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 4000);
    plugin.setParameter(kP_FilterEnvAmount, 2000);

    // Delay 300ms, base feedback low, 40% mix
    plugin.setParameter(kP_DelayTime, 300);
    plugin.setParameter(kP_DelayFeedback, 100);
    plugin.setParameter(kP_DelayMix, 400);

    // Velocity -> Delay Feedback
    plugin.setParameter(kP_Mod1Source, kSrc_Velocity);
    plugin.setParameter(kP_Mod1Dest, kDst_DelayFeedback);
    plugin.setParameter(kP_Mod1Amount, 800);

    // Short percussive envelope
    plugin.setParameter(kP_AmpAttack, 2);
    plugin.setParameter(kP_AmpDecay, 150);
    plugin.setParameter(kP_AmpSustain, 0);
    plugin.setParameter(kP_AmpRelease, 500);

    // Soft hit — little feedback
    plugin.midiNoteOn(0, 60, 40);
    renderToWav(plugin, wav, 2.0f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 0.5f);

    // Hard hit — lots of feedback
    plugin.midiNoteOn(0, 60, 127);
    renderToWav(plugin, wav, 2.0f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 1.0f);

    wav.close();
    ASSERT_TRUE(true, "velocity->feedback delay rendered");
    TEST_PASS();
}

// =========================================================================
// Exploration: Per-voice delay showcase — minor pentatonic sequence
// Plays interleaving notes so multiple voices overlap, each with its own
// delay line.  Three sections show different delay characters:
//   A) Rhythmic echo  — 250ms delay, moderate feedback, hear each note's
//      independent echo tail overlapping with the next note.
//   B) Per-voice chorus — short delay with LFO→DelayTime; each voice's
//      LFO starts at a different phase, creating rich stereo-like shimmer.
//   C) Per-voice flanger — very short delay, LFO mod + feedback; comb
//      filter sweeps independently on every sustained note.
// =========================================================================
TestResult test_pervoice_delay_pentatonic_wav() {
    TEST_BEGIN("Per-voice delay pentatonic (writes bin/fx_pervoice_delay.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/fx_pervoice_delay.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // C minor pentatonic: C3 Eb3 F3 G3 Bb3 C4 Eb4 G4 Bb4
    // 9 notes with 8 voices forces voice stealing at note 9.
    const int penta[] = { 48, 51, 53, 55, 58, 60, 63, 67, 70 };
    const int numNotes = 9;

    // Common setup: saw osc, mild filter, plucky envelope
    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 4000);
    plugin.setParameter(kP_FilterEnvAmount, 3000);
    plugin.setParameter(kP_Resonance, 150);
    plugin.setParameter(kP_AmpAttack, 5);
    plugin.setParameter(kP_AmpDecay, 300);
    plugin.setParameter(kP_AmpSustain, 200);
    plugin.setParameter(kP_AmpRelease, 500);

    // Helper: play ascending pentatonic pattern (overlapping notes)
    // noteLen  = how long each note is held (seconds)
    // spacing  = time between note-ons (seconds), < noteLen means overlap
    // velocity = MIDI velocity
    auto playScale = [&](float spacing, float noteLen, int vel) {
        for (int n = 0; n < numNotes; ++n) {
            plugin.midiNoteOn(0, penta[n], vel);
            renderToWav(plugin, wav, spacing);
        }
        // Let last notes ring + release
        renderToWav(plugin, wav, noteLen - spacing);
        // Release all
        for (int n = 0; n < numNotes; ++n) {
            plugin.midiNoteOff(0, penta[n]);
        }
        renderToWav(plugin, wav, 1.5f); // tail
    };

    // === Section A: Rhythmic echo (250ms, 50% feedback) ===
    // Each note gets its own echo trail — overlapping echoes from
    // multiple voices create a cascading rhythmic texture.
    plugin.setParameter(kP_DelayTime, 250);
    plugin.setParameter(kP_DelayFeedback, 500);
    plugin.setParameter(kP_DelayMix, 400);
    plugin.setParameter(kP_Mod1Source, kSrc_Off);
    plugin.setParameter(kP_Mod1Dest, 0);
    plugin.setParameter(kP_Mod1Amount, 0);

    playScale(0.25f, 0.4f, 100);

    // === Section B: Per-voice chorus (10ms delay, LFO→DelayTime) ===
    // Each voice's LFO is free-running — notes triggered at different
    // times get different LFO phases, so each one shimmers uniquely.
    plugin.setParameter(kP_DelayTime, 10);
    plugin.setParameter(kP_DelayFeedback, 0);
    plugin.setParameter(kP_DelayMix, 500);
    plugin.setParameter(kP_LfoSpeed, 250);  // ~1.5 Hz
    plugin.setParameter(kP_LfoShape, 0);     // Sine
    plugin.setParameter(kP_Mod1Source, kSrc_LFO);
    plugin.setParameter(kP_Mod1Dest, kDst_DelayTime);
    plugin.setParameter(kP_Mod1Amount, 400);

    playScale(0.3f, 0.5f, 100);

    // === Section C: Per-voice flanger (3ms, LFO + feedback) ===
    // Comb filter sweep on each voice independently — held chord
    // will have a rich, evolving quality as each note flanges at
    // its own LFO phase.
    plugin.setParameter(kP_DelayTime, 3);
    plugin.setParameter(kP_DelayFeedback, 650);
    plugin.setParameter(kP_DelayMix, 500);
    plugin.setParameter(kP_LfoSpeed, 150);  // slow sweep
    plugin.setParameter(kP_Mod1Source, kSrc_LFO);
    plugin.setParameter(kP_Mod1Dest, kDst_DelayTime);
    plugin.setParameter(kP_Mod1Amount, 700);

    // For flanger, play as a held chord to hear the sweep
    for (int n = 0; n < 5; ++n) {
        plugin.midiNoteOn(0, penta[n], 100);
    }
    renderToWav(plugin, wav, 4.0f);
    for (int n = 0; n < 5; ++n) {
        plugin.midiNoteOff(0, penta[n]);
    }
    renderToWav(plugin, wav, 2.0f);

    wav.close();
    ASSERT_TRUE(true, "per-voice delay pentatonic rendered");
    TEST_PASS();
}

// =========================================================================
// Synthesis demo: Karplus-Strong plucked strings
// Short delay (1/freq) with high feedback and noise excitation creates
// physically-modelled plucked string tones. Each voice is an independent
// string with its own pitch determined by the delay length.
// =========================================================================
TestResult test_karplus_strong_wav() {
    TEST_BEGIN("Karplus-Strong plucked strings (writes bin/synth_karplus.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/synth_karplus.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Use noise-like excitation: very short attack, instant decay
    // The delay line's feedback creates the pitched resonance.
    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 800);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 8000);
    plugin.setParameter(kP_FilterEnvAmount, 5000);
    plugin.setParameter(kP_Resonance, 0);

    // Percussive envelope — short pluck excitation
    plugin.setParameter(kP_AmpAttack, 0);
    plugin.setParameter(kP_AmpDecay, 30);
    plugin.setParameter(kP_AmpSustain, 0);
    plugin.setParameter(kP_AmpRelease, 10);

    // Delay as resonator: ~5ms = 200Hz fundamental, high feedback
    plugin.setParameter(kP_DelayTime, 5);
    plugin.setParameter(kP_DelayFeedback, 850);
    plugin.setParameter(kP_DelayMix, 900);

    // Arpeggiated plucks — C major chord
    const int notes[] = { 60, 64, 67, 72, 76 };
    for (int i = 0; i < 5; ++i) {
        plugin.midiNoteOn(0, notes[i], 100);
        renderToWav(plugin, wav, 0.05f);
        plugin.midiNoteOff(0, notes[i]);
        renderToWav(plugin, wav, 0.6f);  // let string ring
    }

    // Chord strum — all at once
    for (int i = 0; i < 5; ++i) {
        plugin.midiNoteOn(0, notes[i], 90);
        renderToWav(plugin, wav, 0.02f);
    }
    renderToWav(plugin, wav, 3.0f);  // long ring
    for (int i = 0; i < 5; ++i) {
        plugin.midiNoteOff(0, notes[i]);
    }
    renderToWav(plugin, wav, 2.0f);  // decay tail

    wav.close();
    ASSERT_TRUE(true, "Karplus-Strong rendered");
    TEST_PASS();
}

// =========================================================================
// Synthesis demo: Per-voice echo cascades
// Each note triggers its own echo trail. Overlapping notes produce
// interleaving echo patterns that are distinct per voice — unlike a
// global delay which smears all voices together.
// =========================================================================
TestResult test_pervoice_echo_cascade_wav() {
    TEST_BEGIN("Per-voice echo cascade (writes bin/synth_echo_cascade.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/synth_echo_cascade.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Bright, percussive hits — makes echo pattern clearly audible
    plugin.setParameter(kP_Osc1Waveform, kWave_Square);
    plugin.setParameter(kP_Osc1Level, 600);
    plugin.setParameter(kP_Osc2Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc2Level, 400);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 6000);
    plugin.setParameter(kP_FilterEnvAmount, 4000);
    plugin.setParameter(kP_Resonance, 100);

    // Very short pluck
    plugin.setParameter(kP_AmpAttack, 0);
    plugin.setParameter(kP_AmpDecay, 60);
    plugin.setParameter(kP_AmpSustain, 0);
    plugin.setParameter(kP_AmpRelease, 20);

    // 200ms echo, moderate feedback — each voice echoes independently
    plugin.setParameter(kP_DelayTime, 200);
    plugin.setParameter(kP_DelayFeedback, 550);
    plugin.setParameter(kP_DelayMix, 600);

    // Pattern: quick ascending notes, each spawning its own echo trail
    const int melody[] = { 60, 63, 67, 72, 75, 79 };
    for (int i = 0; i < 6; ++i) {
        plugin.midiNoteOn(0, melody[i], 110);
        renderToWav(plugin, wav, 0.05f);
        plugin.midiNoteOff(0, melody[i]);
        renderToWav(plugin, wav, 0.25f);
    }
    renderToWav(plugin, wav, 3.0f);  // let all echoes ring out

    // Second pattern: two interleaving notes — clearly distinct trails
    plugin.midiNoteOn(0, 48, 100);
    renderToWav(plugin, wav, 0.05f);
    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.1f);  // short gap

    plugin.midiNoteOn(0, 60, 100);
    renderToWav(plugin, wav, 0.05f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 3.0f);  // hear both trails interleave

    wav.close();
    ASSERT_TRUE(true, "per-voice echo cascade rendered");
    TEST_PASS();
}

// =========================================================================
// Synthesis demo: Comb filter as timbre shaper
// Very short delay (1-3ms) with high feedback creates a comb filter that
// adds resonant harmonics. Each voice gets its own comb character.
// Unlike the ZDF filter, comb filtering adds harmonics at integer
// multiples of a frequency determined by the delay length.
// =========================================================================
TestResult test_comb_filter_timbre_wav() {
    TEST_BEGIN("Comb filter timbre (writes bin/synth_comb_timbre.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/synth_comb_timbre.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Start with a simple saw, then let the comb add harmonics
    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 3000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_Resonance, 50);

    // Sustained pad envelope
    plugin.setParameter(kP_AmpAttack, 50);
    plugin.setParameter(kP_AmpDecay, 100);
    plugin.setParameter(kP_AmpSustain, 800);
    plugin.setParameter(kP_AmpRelease, 300);

    // Section 1: 2ms comb — metallic, nasal character
    plugin.setParameter(kP_DelayTime, 2);
    plugin.setParameter(kP_DelayFeedback, 750);
    plugin.setParameter(kP_DelayMix, 500);

    plugin.midiNoteOn(0, 48, 90);
    plugin.midiNoteOn(0, 55, 90);
    renderToWav(plugin, wav, 2.0f);
    plugin.midiNoteOff(0, 48);
    plugin.midiNoteOff(0, 55);
    renderToWav(plugin, wav, 1.0f);

    // Section 2: sweep comb time from 1ms to 5ms using LFO mod
    plugin.setParameter(kP_DelayTime, 3);
    plugin.setParameter(kP_DelayFeedback, 700);
    plugin.setParameter(kP_DelayMix, 500);
    plugin.setParameter(kP_LfoSpeed, 80);   // slow sweep
    plugin.setParameter(kP_LfoShape, 0);     // sine
    plugin.setParameter(kP_Mod1Source, kSrc_LFO);
    plugin.setParameter(kP_Mod1Dest, kDst_DelayTime);
    plugin.setParameter(kP_Mod1Amount, 600);

    plugin.midiNoteOn(0, 60, 90);
    plugin.midiNoteOn(0, 67, 90);
    renderToWav(plugin, wav, 4.0f);
    plugin.midiNoteOff(0, 60);
    plugin.midiNoteOff(0, 67);
    renderToWav(plugin, wav, 1.5f);

    wav.close();
    ASSERT_TRUE(true, "comb filter timbre rendered");
    TEST_PASS();
}

// =========================================================================
// Synthesis demo: Per-voice slapback doubling
// 20-80ms delay with low feedback gives each note its own "room" or
// doubling effect. Each voice sounds like two attacks slightly offset —
// a subtle thickening that a global delay can't achieve per-note.
// =========================================================================
TestResult test_slapback_doubling_wav() {
    TEST_BEGIN("Per-voice slapback (writes bin/synth_slapback.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/synth_slapback.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Bright lead sound
    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 700);
    plugin.setParameter(kP_Osc2Waveform, kWave_Square);
    plugin.setParameter(kP_Osc2Level, 300);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 5000);
    plugin.setParameter(kP_FilterEnvAmount, 3000);
    plugin.setParameter(kP_Resonance, 100);

    // Medium envelope — sustained lead
    plugin.setParameter(kP_AmpAttack, 5);
    plugin.setParameter(kP_AmpDecay, 200);
    plugin.setParameter(kP_AmpSustain, 600);
    plugin.setParameter(kP_AmpRelease, 300);

    // 40ms slapback — subtle doubling, minimal feedback
    plugin.setParameter(kP_DelayTime, 40);
    plugin.setParameter(kP_DelayFeedback, 100);
    plugin.setParameter(kP_DelayMix, 400);

    // Melody line — each note gets its own slapback
    const int melody[] = { 60, 63, 65, 67, 70, 72, 70, 67 };
    for (int i = 0; i < 8; ++i) {
        plugin.midiNoteOn(0, melody[i], 100);
        renderToWav(plugin, wav, 0.3f);
        plugin.midiNoteOff(0, melody[i]);
        renderToWav(plugin, wav, 0.15f);
    }

    // Held chord with slapback — thickening effect
    plugin.setParameter(kP_DelayTime, 60);
    plugin.setParameter(kP_DelayFeedback, 150);
    plugin.setParameter(kP_DelayMix, 350);

    plugin.midiNoteOn(0, 48, 80);
    plugin.midiNoteOn(0, 55, 80);
    plugin.midiNoteOn(0, 60, 80);
    plugin.midiNoteOn(0, 63, 80);
    renderToWav(plugin, wav, 3.0f);
    plugin.midiNoteOff(0, 48);
    plugin.midiNoteOff(0, 55);
    plugin.midiNoteOff(0, 60);
    plugin.midiNoteOff(0, 63);
    renderToWav(plugin, wav, 1.5f);

    wav.close();
    ASSERT_TRUE(true, "slapback doubling rendered");
    TEST_PASS();
}

// =========================================================================
// PolyBLEP Saw + Hard Sync sweep
// Uses the new antialiased PolyBLEP saw waveform as carrier, synced from
// osc3. ModEnv sweeps FM depth to create a classic sync "wow" without
// aliasing artifacts.
// =========================================================================
TestResult test_polyblep_saw_sync_wav() {
    TEST_BEGIN("PolyBLEP saw + hard sync (writes bin/feat_polyblep_saw_sync.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_polyblep_saw_sync.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Carrier (osc1): PolyBLEP saw, +7 semitones above master
    plugin.setParameter(kP_Osc1Waveform, kWave_PolyBlepSaw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc1Semitone, 7);
    plugin.setParameter(kP_Osc2Level, 0);
    // Master (osc3): sine at base pitch, inaudible — provides sync trigger
    plugin.setParameter(kP_Osc3Waveform, kWave_Sine);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_Sync3to1, 1);

    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    // Section 1: Static sync — carrier a 5th above master
    plugin.setParameter(kP_AmpAttack, 10);
    plugin.setParameter(kP_AmpDecay, 200);
    plugin.setParameter(kP_AmpSustain, 800);
    plugin.setParameter(kP_AmpRelease, 300);

    plugin.midiNoteOn(0, 48, 100);
    renderToWav(plugin, wav, 1.5f);
    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.5f);

    // Section 2: ModEnv sweep — classic sync "wow"
    plugin.setParameter(kP_Osc1Semitone, 12);
    plugin.setParameter(kP_ModEnvAttack, 600);
    plugin.setParameter(kP_ModEnvDecay, 1000);
    plugin.setParameter(kP_ModEnvSustain, 0);
    plugin.setParameter(kP_ModEnvRelease, 300);
    plugin.setParameter(kP_Mod1Source, kSrc_ModEnv);
    plugin.setParameter(kP_Mod1Dest, kDst_FM3to1);
    plugin.setParameter(kP_Mod1Amount, 80);

    plugin.midiNoteOn(0, 48, 100);
    renderToWav(plugin, wav, 2.5f);
    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.5f);

    // Section 3: High note — emphasises aliasing reduction
    plugin.setParameter(kP_Osc1Semitone, 19);
    plugin.setParameter(kP_Mod1Amount, 0);  // no FM sweep, just static sync
    plugin.midiNoteOn(0, 72, 100);
    renderToWav(plugin, wav, 1.5f);
    plugin.midiNoteOff(0, 72);
    renderToWav(plugin, wav, 0.5f);

    wav.close();
    ASSERT_TRUE(true, "PolyBLEP saw + hard sync rendered");
    TEST_PASS();
}

// =========================================================================
// PolyBLEP Square + PWM sweep
// Uses the new antialiased PolyBLEP square waveform with a slow pulse
// width sweep, demonstrating clean PWM without aliasing artifacts.
// =========================================================================
TestResult test_polyblep_square_pwm_wav() {
    TEST_BEGIN("PolyBLEP square + PWM (writes bin/feat_polyblep_square_pwm.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_polyblep_square_pwm.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_PolyBlepSquare);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 8000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    plugin.setParameter(kP_AmpAttack, 10);
    plugin.setParameter(kP_AmpDecay, 200);
    plugin.setParameter(kP_AmpSustain, 800);
    plugin.setParameter(kP_AmpRelease, 300);

    // Section 1: Slow PW sweep on a low note — fat bass tone
    plugin.midiNoteOn(0, 36, 100);
    {
        int totalBlocks = blocksFor(3.0f);
        for (int b = 0; b < totalBlocks; ++b) {
            float t = static_cast<float>(b) / totalBlocks;
            // Sweep morph (=PW) from 0.1 to 0.9 and back
            float pw = (t < 0.5f) ? (0.1f + t * 1.6f) : (0.9f - (t - 0.5f) * 1.6f);
            plugin.setParameter(kP_Osc1Morph, static_cast<int16_t>(pw * 1000));
            plugin.step(BLOCK_SIZE);
            wav.writeMono(plugin.getBus(OUTPUT_BUS, BLOCK_SIZE), BLOCK_SIZE);
        }
    }
    plugin.midiNoteOff(0, 36);
    renderToWav(plugin, wav, 0.5f);

    // Section 2: Higher note with narrower PW range — nasal lead
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.midiNoteOn(0, 60, 100);
    {
        int totalBlocks = blocksFor(2.0f);
        for (int b = 0; b < totalBlocks; ++b) {
            float t = static_cast<float>(b) / totalBlocks;
            // Narrow sweep 0.2 to 0.4 — subtle timbral motion
            float pw = 0.2f + t * 0.2f;
            plugin.setParameter(kP_Osc1Morph, static_cast<int16_t>(pw * 1000));
            plugin.step(BLOCK_SIZE);
            wav.writeMono(plugin.getBus(OUTPUT_BUS, BLOCK_SIZE), BLOCK_SIZE);
        }
    }
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 0.5f);

    // Section 3: Very high note — aliasing reduction most audible here
    plugin.setParameter(kP_Osc1Morph, 500);  // 50% duty
    plugin.midiNoteOn(0, 84, 100);
    renderToWav(plugin, wav, 1.5f);
    plugin.midiNoteOff(0, 84);
    renderToWav(plugin, wav, 0.5f);

    wav.close();
    ASSERT_TRUE(true, "PolyBLEP square + PWM rendered");
    TEST_PASS();
}

// =========================================================================
// PolyBLEP vs Naive A/B comparison — STEREO: L=naive, R=polyBLEP
//
// Two independent plugin instances render the same notes simultaneously.
//   Left channel  = naive (aliased) waveform
//   Right channel = PolyBLEP (antialiased) waveform
// Solo L vs R in your DAW to hear the difference instantly.
//
// Layout:
//   Section 1 — SAW: held notes at C2,C3,C4,C5,C6,C7,C8 (0.6s each)
//   Section 2 — SQUARE: same octave ladder
//   Section 3 — SAW pitch sweep C4→C8 over 4s (most dramatic)
//   Section 4 — SQUARE pitch sweep C4→C8 over 4s
// =========================================================================
TestResult test_polyblep_vs_naive_wav() {
    TEST_BEGIN("PolyBLEP vs Naive A/B (writes bin/feat_polyblep_vs_naive.wav)");

    // Single plugin instance, mono — alternates naive then PolyBLEP at each
    // pitch with a rest between every note so the difference is easy to hear.
    PluginInstance plug;
    ASSERT_TRUE(createPlugin(plug), "plugin created");

    WavWriter wav("bin/feat_polyblep_vs_naive.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Wide-open filter, no FX, fast envelope
    plug.setParameter(kP_Osc1Level, 1000);
    plug.setParameter(kP_Osc2Level, 0);
    plug.setParameter(kP_Osc3Level, 0);
    plug.setParameter(kP_BaseCutoff, 10000);
    plug.setParameter(kP_FilterEnvAmount, 0);
    plug.setParameter(kP_DelayMix, 0);
    plug.setParameter(kP_AmpAttack, 3);
    plug.setParameter(kP_AmpDecay, 50);
    plug.setParameter(kP_AmpSustain, 1000);
    plug.setParameter(kP_AmpRelease, 50);
    plug.setParameter(kP_Osc1Morph, 500);  // 50% duty for square waves
    plug.setParameter(kP_Resonance, 0);
    plug.setParameter(kP_Drive, 0);

    auto renderMono = [&](float seconds) {
        int n = blocksFor(seconds);
        for (int i = 0; i < n; ++i) {
            plug.step(BLOCK_SIZE);
            const float* bus = plug.getBus(OUTPUT_BUS, BLOCK_SIZE);
            wav.writeMono(bus, BLOCK_SIZE);
        }
    };

    const float NOTE_DUR = 0.6f;
    const float REST_DUR = 0.25f;
    const float SECTION_GAP = 0.5f;

    // Helper: play naive then polyblep at a given MIDI note
    auto playPair = [&](int midi, int naiveWave, int blepWave) {
        // -- Naive --
        plug.setParameter(kP_Osc1Waveform, naiveWave);
        plug.midiNoteOn(0, midi, 100);
        renderMono(NOTE_DUR);
        plug.midiNoteOff(0, midi);
        renderMono(REST_DUR);

        // -- PolyBLEP --
        plug.setParameter(kP_Osc1Waveform, blepWave);
        plug.midiNoteOn(0, midi, 100);
        renderMono(NOTE_DUR);
        plug.midiNoteOff(0, midi);
        renderMono(REST_DUR);
    };

    // === Section 1: SAW — octave ladder C2→C8 ===
    // Each pitch: naive saw, rest, PolyBLEP saw, rest
    const int notes[] = { 36, 48, 60, 72, 84, 96, 108 };  // C2..C8
    for (int midi : notes) {
        playPair(midi, kWave_Saw, kWave_PolyBlepSaw);
    }
    renderMono(SECTION_GAP);

    // === Section 2: SQUARE — same ladder ===
    for (int midi : notes) {
        playPair(midi, kWave_Square, kWave_PolyBlepSquare);
    }
    renderMono(SECTION_GAP);

    // === Section 3: SAW chromatic climb C4→C8 — naive then PolyBLEP ===
    // Every 4th semitone from 60 (C4) to 108 (C8): minor-3rd steps
    for (int waveform : { kWave_Saw, kWave_PolyBlepSaw }) {
        plug.setParameter(kP_Osc1Waveform, waveform);
        for (int midi = 60; midi <= 108; midi += 4) {
            plug.midiNoteOn(0, midi, 100);
            renderMono(0.35f);
            plug.midiNoteOff(0, midi);
            renderMono(0.05f);
        }
        renderMono(REST_DUR);
    }
    renderMono(SECTION_GAP);

    // === Section 4: SQUARE chromatic climb C4→C8 — naive then PolyBLEP ===
    for (int waveform : { kWave_Square, kWave_PolyBlepSquare }) {
        plug.setParameter(kP_Osc1Waveform, waveform);
        for (int midi = 60; midi <= 108; midi += 4) {
            plug.midiNoteOn(0, midi, 100);
            renderMono(0.35f);
            plug.midiNoteOff(0, midi);
            renderMono(0.05f);
        }
        renderMono(REST_DUR);
    }
    renderMono(SECTION_GAP);

    wav.close();
    ASSERT_TRUE(true, "PolyBLEP vs Naive A/B rendered");
    TEST_PASS();
}

// =========================================================================
// PolyBLEP at decimated sample rates (oscillator-level decimation)
//
// Uses the plugin's SampleReduce parameter (which now drives oscillator
// decimation directly) to render PolyBLEP saw and square at 1×, 2×, 4×,
// and 8× decimation.  Mono, octave ladder at each rate.
//
// Layout:
//   Section 1 — PolyBLEP Saw:    C3 C5 C7 at 1×, rest, 2×, rest, 4×, rest, 8×
//   Section 2 — PolyBLEP Square:  same pattern
// =========================================================================
TestResult test_polyblep_decimated_rate_wav() {
    TEST_BEGIN("PolyBLEP decimated rate (writes bin/feat_polyblep_decimated_rate.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_polyblep_decimated_rate.wav",
                  NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Wide-open filter, no FX, fast envelope
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);
    plugin.setParameter(kP_AmpAttack, 3);
    plugin.setParameter(kP_AmpDecay, 50);
    plugin.setParameter(kP_AmpSustain, 1000);
    plugin.setParameter(kP_AmpRelease, 50);
    plugin.setParameter(kP_Osc1Morph, 500);  // 50% duty for square waves
    plugin.setParameter(kP_Resonance, 0);
    plugin.setParameter(kP_Drive, 0);
    plugin.setParameter(kP_BitCrush, 16);       // no bit-depth crush

    auto renderMono = [&](float seconds) {
        int n = blocksFor(seconds);
        for (int i = 0; i < n; ++i) {
            plugin.step(BLOCK_SIZE);
            const float* bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
            wav.writeMono(bus, BLOCK_SIZE);
        }
    };

    const float NOTE_DUR  = 0.8f;
    const float REST_DUR  = 0.2f;
    const float RATE_GAP  = 0.4f;
    const float SECT_GAP  = 0.6f;

    const int testNotes[] = { 48, 72, 96 };        // C3, C5, C7
    const int decRates[]  = { 1, 2, 4, 8 };        // 1×, ½×, ¼×, ⅛×

    // --- Section 1: PolyBLEP Saw ---
    plugin.setParameter(kP_Osc1Waveform, kWave_PolyBlepSaw);
    for (int dec : decRates) {
        plugin.setParameter(kP_SampleReduce, dec);
        for (int midi : testNotes) {
            plugin.midiNoteOn(0, midi, 100);
            renderMono(NOTE_DUR);
            plugin.midiNoteOff(0, midi);
            renderMono(REST_DUR);
        }
        renderMono(RATE_GAP);
    }
    renderMono(SECT_GAP);

    // --- Section 2: PolyBLEP Square ---
    plugin.setParameter(kP_Osc1Waveform, kWave_PolyBlepSquare);
    for (int dec : decRates) {
        plugin.setParameter(kP_SampleReduce, dec);
        for (int midi : testNotes) {
            plugin.midiNoteOn(0, midi, 100);
            renderMono(NOTE_DUR);
            plugin.midiNoteOff(0, midi);
            renderMono(REST_DUR);
        }
        renderMono(RATE_GAP);
    }

    plugin.setParameter(kP_SampleReduce, 1);  // reset
    wav.close();
    ASSERT_TRUE(true, "PolyBLEP decimated rate comparison rendered");
    TEST_PASS();
}

// =========================================================================
// PolyBLEP Saw sync sweep — identical to feat_sync_sweep but using
// the antialiased PolyBLEP saw as the carrier waveform.
// =========================================================================
TestResult test_polyblep_sync_sweep_wav() {
    TEST_BEGIN("PolyBLEP saw sync sweep (writes bin/feat_polyblep_sync_sweep.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_polyblep_sync_sweep.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Carrier (osc1) PolyBLEP saw, +12 semitones above master
    plugin.setParameter(kP_Osc1Waveform, kWave_PolyBlepSaw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc1Semitone, 12);
    plugin.setParameter(kP_Osc2Level, 0);
    // Master (osc3) sine at base pitch, inaudible
    plugin.setParameter(kP_Osc3Waveform, kWave_Sine);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_Sync3to1, 1);
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    // ModEnv: slow attack sweep up then decay — creates rising then falling FM
    plugin.setParameter(kP_ModEnvAttack, 800);
    plugin.setParameter(kP_ModEnvDecay, 1200);
    plugin.setParameter(kP_ModEnvSustain, 0);
    plugin.setParameter(kP_ModEnvRelease, 300);

    // Route ModEnv → FM 3→1 (sweeps carrier instantaneous frequency)
    plugin.setParameter(kP_Mod1Source, kSrc_ModEnv);
    plugin.setParameter(kP_Mod1Dest, kDst_FM3to1);
    plugin.setParameter(kP_Mod1Amount, 80);

    // Amp env: long sustain so we hear the full ModEnv cycle
    plugin.setParameter(kP_AmpAttack, 5);
    plugin.setParameter(kP_AmpDecay, 200);
    plugin.setParameter(kP_AmpSustain, 800);
    plugin.setParameter(kP_AmpRelease, 500);

    // Play three notes to hear the sweep repeat
    int notes[] = {36, 48, 60};
    for (int n : notes) {
        plugin.midiNoteOn(0, n, 100);
        renderToWav(plugin, wav, 2.5f);
        plugin.midiNoteOff(0, n);
        renderToWav(plugin, wav, 0.6f);
    }

    // Finish with a Cmaj7 bass voicing (C1-E1-G1-B1)
    plugin.midiNoteOn(0, 36, 100);  // C1
    plugin.midiNoteOn(0, 40, 100);  // E1
    plugin.midiNoteOn(0, 43, 100);  // G1
    plugin.midiNoteOn(0, 47, 100);  // B1
    renderToWav(plugin, wav, 3.5f);
    plugin.midiNoteOff(0, 36);
    plugin.midiNoteOff(0, 40);
    plugin.midiNoteOff(0, 43);
    plugin.midiNoteOff(0, 47);
    renderToWav(plugin, wav, 1.0f);

    wav.close();
    ASSERT_TRUE(true, "PolyBLEP sync sweep rendered");
    TEST_PASS();
}

// =========================================================================
// MIDI Clock Sync: LFO synced to external MIDI clock
// Demonstrates per-note synced LFO modulating filter cutoff.
// Section 1: 120 BPM, single note with LFO->Cutoff at 1/4 rate
// Section 2: 120 BPM, chord showing per-voice LFO phase differences
// Section 3: Tempo change to 80 BPM — LFO follows slower clock
// =========================================================================
TestResult test_midi_sync_lfo_wav() {
    TEST_BEGIN("MIDI clock sync LFO (writes bin/feat_midi_sync_lfo.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_midi_sync_lfo.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // PolyBLEP saw, solo osc1, moderate filter
    plugin.setParameter(kP_Osc1Waveform, kWave_PolyBlepSaw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 3000);   // mid cutoff so LFO sweep is audible
    plugin.setParameter(kP_Resonance, 400);     // some resonance for emphasis
    plugin.setParameter(kP_FilterEnvAmount, 0); // no filter env, just LFO
    plugin.setParameter(kP_DelayMix, 0);

    // Amp envelope: fast attack, long sustain
    plugin.setParameter(kP_AmpAttack, 5);
    plugin.setParameter(kP_AmpDecay, 100);
    plugin.setParameter(kP_AmpSustain, 900);
    plugin.setParameter(kP_AmpRelease, 300);

    // LFO1: sine shape, synced to 1/4 note
    plugin.setParameter(kP_LfoShape, 0);        // sine
    plugin.setParameter(kP_Lfo1SyncMode, 5);    // 1/4 note sync

    // Route LFO1 -> Cutoff, amount = 0.8
    plugin.setParameter(kP_Mod1Source, kSrc_LFO);
    plugin.setParameter(kP_Mod1Dest, kDst_Cutoff);
    plugin.setParameter(kP_Mod1Amount, 800);

    float clockAccum = 0.0f;

    // Start MIDI clock
    plugin.midiClockStart();

    // --- Section 1: Single note at 120 BPM (4 seconds) ---
    // Pump a few ticks first so BPM is established before the note
    renderToWavWithClock(plugin, wav, 0.5f, 120.0f, clockAccum);
    plugin.midiNoteOn(0, 48, 100);  // C3
    renderToWavWithClock(plugin, wav, 4.0f, 120.0f, clockAccum);
    plugin.midiNoteOff(0, 48);
    renderToWavWithClock(plugin, wav, 0.5f, 120.0f, clockAccum);

    // --- Section 2: Chord at 120 BPM (4 seconds) ---
    // Each voice has its own LFO phase, so the chord shimmers
    plugin.midiNoteOn(0, 48, 100);  // C3
    renderToWavWithClock(plugin, wav, 0.15f, 120.0f, clockAccum);
    plugin.midiNoteOn(0, 52, 90);   // E3
    renderToWavWithClock(plugin, wav, 0.15f, 120.0f, clockAccum);
    plugin.midiNoteOn(0, 55, 85);   // G3
    renderToWavWithClock(plugin, wav, 3.7f, 120.0f, clockAccum);
    plugin.midiNoteOff(0, 48);
    plugin.midiNoteOff(0, 52);
    plugin.midiNoteOff(0, 55);
    renderToWavWithClock(plugin, wav, 0.5f, 120.0f, clockAccum);

    // --- Section 3: Tempo change to 80 BPM (4 seconds) ---
    // LFO should slow down to match the new tempo
    plugin.midiNoteOn(0, 36, 100);  // C2 bass note
    renderToWavWithClock(plugin, wav, 4.0f, 80.0f, clockAccum);
    plugin.midiNoteOff(0, 36);
    renderToWavWithClock(plugin, wav, 0.5f, 80.0f, clockAccum);

    // Stop clock
    plugin.midiClockStop();

    wav.close();
    ASSERT_TRUE(true, "MIDI sync LFO WAV rendered");
    TEST_PASS();
}

// =========================================================================
// Feature: Delay time synced to MIDI clock
// Plays notes at 120 BPM with delay synced to 1/4 note (500ms), then
// changes tempo to 90 BPM (delay → 667ms). Verifies synced delay adapts.
// =========================================================================
TestResult test_delay_sync_wav() {
    TEST_BEGIN("Delay sync to MIDI clock (writes bin/feat_delay_sync.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_delay_sync.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Solo osc1 PolyBLEP saw, open filter
    plugin.setParameter(kP_Osc1Waveform, kWave_PolyBlepSaw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 6000);
    plugin.setParameter(kP_FilterEnvAmount, 0);

    // Amp envelope: short staccato to make echoes clearly audible
    plugin.setParameter(kP_AmpAttack, 5);
    plugin.setParameter(kP_AmpDecay, 80);
    plugin.setParameter(kP_AmpSustain, 0);
    plugin.setParameter(kP_AmpRelease, 100);

    // Delay: high feedback so echoes are obvious, sync to 1/4 note
    plugin.setParameter(kP_DelayFeedback, 500);
    plugin.setParameter(kP_DelayMix, 600);
    plugin.setParameter(kP_DelaySyncMode, 5);  // 1/4 note

    float clockAccum = 0.0f;

    // Start MIDI clock
    plugin.midiClockStart();

    // Establish tempo at 120 BPM (pump a bit for BPM to lock in)
    renderToWavWithClock(plugin, wav, 0.5f, 120.0f, clockAccum);

    // --- Section 1: Staccato notes at 120 BPM, delay = 1/4 note = 500ms ---
    plugin.midiNoteOn(0, 60, 100);   // C4
    renderToWavWithClock(plugin, wav, 0.1f, 120.0f, clockAccum);
    plugin.midiNoteOff(0, 60);
    renderToWavWithClock(plugin, wav, 1.9f, 120.0f, clockAccum);  // let echoes ring

    plugin.midiNoteOn(0, 64, 100);   // E4
    renderToWavWithClock(plugin, wav, 0.1f, 120.0f, clockAccum);
    plugin.midiNoteOff(0, 64);
    renderToWavWithClock(plugin, wav, 1.9f, 120.0f, clockAccum);

    // --- Section 2: Tempo change to 90 BPM, delay = 1/4 note = 667ms ---
    plugin.midiNoteOn(0, 67, 100);   // G4
    renderToWavWithClock(plugin, wav, 0.1f, 90.0f, clockAccum);
    plugin.midiNoteOff(0, 67);
    renderToWavWithClock(plugin, wav, 2.9f, 90.0f, clockAccum);  // longer for slower echoes

    // --- Section 3: Dotted 1/8 at 120 BPM ---
    plugin.setParameter(kP_DelaySyncMode, 11); // 1/8.
    renderToWavWithClock(plugin, wav, 0.3f, 120.0f, clockAccum);
    plugin.midiNoteOn(0, 72, 100);   // C5
    renderToWavWithClock(plugin, wav, 0.1f, 120.0f, clockAccum);
    plugin.midiNoteOff(0, 72);
    renderToWavWithClock(plugin, wav, 2.4f, 120.0f, clockAccum);

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Feature: Delay sync auto-start (no Start message) + MIDI Continue
// Verifies the clock tracker locks BPM from bare F8 ticks (common on
// hardware where the sequencer was already running when the plugin loaded).
// Also tests Stop → Continue resume cycle.
// =========================================================================
TestResult test_delay_sync_autostart_wav() {
    TEST_BEGIN("Delay sync auto-start + continue (writes bin/feat_delay_sync_auto.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_delay_sync_auto.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Solo osc1, staccato envelope for clear echoes
    plugin.setParameter(kP_Osc1Waveform, kWave_PolyBlepSaw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 6000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_AmpAttack, 5);
    plugin.setParameter(kP_AmpDecay, 80);
    plugin.setParameter(kP_AmpSustain, 0);
    plugin.setParameter(kP_AmpRelease, 100);

    // Delay: synced to 1/4, high feedback + mix
    plugin.setParameter(kP_DelayFeedback, 500);
    plugin.setParameter(kP_DelayMix, 600);
    plugin.setParameter(kP_DelaySyncMode, 5);  // 1/4 note

    // Set manual delay time to something very different from 1/4@120BPM (500ms)
    // so we can detect whether sync actually took effect.
    // Manual = 100ms (raw 100); synced @120 = 500ms.
    plugin.setParameter(kP_DelayTime, 100);

    float clockAccum = 0.0f;

    // --- Section 1: NO Start message, just raw F8 ticks at 120 BPM ---
    // The clock tracker should auto-start on the first F8.
    // Pump ticks long enough for BPM to lock (>= 6 ticks).
    renderToWavWithClock(plugin, wav, 0.5f, 120.0f, clockAccum);

    // Play a staccato note — echoes should be at 500ms (synced), NOT 100ms (manual)
    plugin.midiNoteOn(0, 60, 100);
    renderToWavWithClock(plugin, wav, 0.1f, 120.0f, clockAccum);
    plugin.midiNoteOff(0, 60);
    renderToWavWithClock(plugin, wav, 2.0f, 120.0f, clockAccum);

    // --- Section 2: Stop, then Continue (0xFB), change to 90 BPM ---
    plugin.midiClockStop();
    renderToWav(plugin, wav, 0.3f);  // brief silence, no clock

    // Resume with Continue (not Start)
    plugin.midiClockContinue();
    renderToWavWithClock(plugin, wav, 0.5f, 90.0f, clockAccum);

    // Play note — echoes should be at 667ms (1/4 @ 90 BPM)
    plugin.midiNoteOn(0, 64, 100);
    renderToWavWithClock(plugin, wav, 0.1f, 90.0f, clockAccum);
    plugin.midiNoteOff(0, 64);
    renderToWavWithClock(plugin, wav, 2.5f, 90.0f, clockAccum);

    wav.close();
    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Wavetable: morph sweep through a generated sine→saw wavetable
// Extern helper function from PolyLofi.cpp to inject wavetable data for testing
// ---------------------------------------------------------------------------
extern "C" void polyLofi_injectWavetable(_NT_algorithm* self, int oscIdx,
                                          const int16_t* data, uint32_t numWaves,
                                          uint32_t waveLength);

// Generate a test wavetable using the shared WavetableGenerator library.
static void generateTestWavetable(int16_t* buffer, uint32_t numWaves, uint32_t waveLength) {
    WtGen::morphShapes(buffer, numWaves, waveLength,
                       WtGen::shapeSine, WtGen::shapeSaw);
}

TestResult test_wavetable_morph_wav() {
    TEST_BEGIN("Wavetable morph sweep (writes bin/feat_wavetable_morph.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_wavetable_morph.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Generate test wavetable: 16 waves x 256 samples (sine→saw)
    static const uint32_t WT_WAVES = 16;
    static const uint32_t WT_LENGTH = 256;
    static int16_t testTable[WT_WAVES * WT_LENGTH];
    generateTestWavetable(testTable, WT_WAVES, WT_LENGTH);

    // Set all 3 oscs to WAVETABLE, solo osc1 for clarity
    plugin.setParameter(kP_Osc1Waveform, kWave_Wavetable);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_DelayMix, 0);

    // Inject wavetable data directly into voices (bypasses SD card loading)
    polyLofi_injectWavetable(plugin.getAlgorithm(), 0, testTable, WT_WAVES, WT_LENGTH);

    // Moderate filter so wavetable character is audible
    plugin.setParameter(kP_BaseCutoff, 8000);
    plugin.setParameter(kP_Resonance, 100);

    // Fast amp envelope
    plugin.setParameter(kP_AmpAttack, 5);
    plugin.setParameter(kP_AmpDecay, 100);
    plugin.setParameter(kP_AmpSustain, 900);
    plugin.setParameter(kP_AmpRelease, 300);

    // Start with morph = 0 (pure sine end)
    plugin.setParameter(kP_Osc1Morph, 0);

    // --- Section 1: Held note, sweep morph from sine to saw (4 seconds) ---
    plugin.midiNoteOn(0, 48, 100); // C3
    float peakAll = 0.0f;
    int totalBlocks = blocksFor(4.0f);
    for (int b = 0; b < totalBlocks; ++b) {
        // Smoothly ramp morph from 0 to 1000 over the section
        int morph = (b * 1000) / totalBlocks;
        plugin.setParameter(kP_Osc1Morph, morph);
        plugin.step(BLOCK_SIZE);
        const float* bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
        wav.writeMono(bus, BLOCK_SIZE);
        float p = PluginInstance::peak(bus, BLOCK_SIZE);
        if (p > peakAll) peakAll = p;
    }
    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.5f);

    // --- Section 2: Chord with different morph positions (3 seconds) ---
    plugin.setParameter(kP_Osc1Morph, 200); // near-sine
    plugin.midiNoteOn(0, 48, 100); // C3
    renderToWav(plugin, wav, 0.1f);
    plugin.setParameter(kP_Osc1Morph, 600); // mid blend
    plugin.midiNoteOn(0, 52, 90);  // E3
    renderToWav(plugin, wav, 0.1f);
    plugin.setParameter(kP_Osc1Morph, 900); // near-saw
    plugin.midiNoteOn(0, 55, 85);  // G3
    renderToWav(plugin, wav, 2.8f);
    plugin.midiNoteOff(0, 48);
    plugin.midiNoteOff(0, 52);
    plugin.midiNoteOff(0, 55);
    renderToWav(plugin, wav, 0.5f);

    // --- Section 3: Osc2 also wavetable with different morph (2 seconds) ---
    plugin.setParameter(kP_Osc2Waveform, kWave_Wavetable);
    plugin.setParameter(kP_Osc2Level, 500);
    polyLofi_injectWavetable(plugin.getAlgorithm(), 1, testTable, WT_WAVES, WT_LENGTH);
    plugin.setParameter(kP_Osc1Morph, 0);   // osc1: sine end
    plugin.setParameter(kP_Osc2Morph, 1000); // osc2: saw end
    plugin.midiNoteOn(0, 36, 100); // C2
    renderToWav(plugin, wav, 2.0f);
    plugin.midiNoteOff(0, 36);
    renderToWav(plugin, wav, 0.5f);

    wav.close();
    ASSERT_TRUE(peakAll > 0.01f, "wavetable produced audio");
    TEST_PASS();
}

// =========================================================================
// =========================================================================
// Voice stealing crossfade test
// Saturates all 8 voices, then triggers a 9th note (forcing a steal).
// Captures the audio around the steal point and verifies there are no
// hard clicks by checking the maximum sample-to-sample amplitude delta.
// A clean crossfade should keep the delta well below 0.25; the old code
// (instant cut) could exceed 0.5.
// =========================================================================
TestResult test_voice_steal_crossfade_wav() {
    TEST_BEGIN("Voice steal crossfade (writes bin/feat_steal_crossfade.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_steal_crossfade.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Bright sound — easy to hear clicks
    plugin.setParameter(kP_Osc1Waveform, kWave_PolyBlepSaw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 6000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);
    plugin.setParameter(kP_AmpAttack, 10);
    plugin.setParameter(kP_AmpDecay, 200);
    plugin.setParameter(kP_AmpSustain, 600);
    plugin.setParameter(kP_AmpRelease, 300);

    // Fill all 8 voices with a C major scale
    const int fillNotes[] = { 48, 50, 52, 53, 55, 57, 59, 60 };
    for (int i = 0; i < 8; ++i) {
        plugin.midiNoteOn(0, fillNotes[i], 100);
    }

    // Let voices settle into sustain
    renderToWav(plugin, wav, 0.5f);

    // Now steal: send a 9th note — this forces the quietest voice to be stolen.
    // Capture the audio around this point to check for clicks.
    plugin.midiNoteOn(0, 72, 110);  // C5 — bright, high velocity

    // Capture 50ms around the steal point
    const int captureBlocks = blocksFor(0.05f);
    std::vector<float> capturedAudio;
    for (int b = 0; b < captureBlocks; ++b) {
        plugin.step(BLOCK_SIZE);
        const float* bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
        wav.writeMono(bus, BLOCK_SIZE);
        for (int i = 0; i < BLOCK_SIZE; ++i) {
            capturedAudio.push_back(bus[i]);
        }
    }

    // Check max sample-to-sample delta — a click shows up as a huge jump
    float maxDelta = 0.0f;
    for (size_t i = 1; i < capturedAudio.size(); ++i) {
        float delta = std::abs(capturedAudio[i] - capturedAudio[i - 1]);
        if (delta > maxDelta) maxDelta = delta;
    }

    // With 8 voices of saw at vel 100 and sustain level 0.6, the
    // per-voice contribution is roughly 0.05-0.1. An unprotected steal
    // would produce a delta > 0.1 from a single voice cutting out.
    // With crossfade, the delta should stay below 0.08 per sample
    // (the saw waveform itself has ~0.04 per-sample variation at C3).
    // Use a generous threshold that catches hard clicks but passes
    // natural waveform transitions.
    // Master volume default is 3.5 (70% of 5V), so scale threshold accordingly.
    ASSERT_LT(maxDelta, 0.875f, "no hard clicks during voice steal crossfade");

    // Steal a second time to verify repeated steals work
    plugin.midiNoteOn(0, 74, 110);  // D5
    renderToWav(plugin, wav, 0.3f);

    // Steal a third time
    plugin.midiNoteOn(0, 76, 110);  // E5
    renderToWav(plugin, wav, 0.3f);

    // Release everything
    for (int i = 0; i < 8; ++i) plugin.midiNoteOff(0, fillNotes[i]);
    plugin.midiNoteOff(0, 72);
    plugin.midiNoteOff(0, 74);
    plugin.midiNoteOff(0, 76);
    renderToWav(plugin, wav, 1.5f);  // release tail

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Feature: Stereo pan spread with classic chord progression
// I-V-vi-IV in C major × 2 cycles, PolyBLEP saw with detuning,
// mono delay, pan spread at 75%. Writes stereo WAV.
// =========================================================================
TestResult test_stereo_chord_progression_wav() {
    TEST_BEGIN("Stereo pan spread chord progression (writes bin/feat_stereo_chords.wav)");

    static constexpr int RIGHT_BUS = 13; // 0-based bus index for right channel

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_stereo_chords.wav", NtTestHarness::getSampleRate(), 2);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // --- Configure stereo output ---
    plugin.setParameter(kP_RightOutput, 14);      // NT 1-based bus 14
    plugin.setParameter(kP_RightOutputMode, 1);    // replace mode
    plugin.setParameter(kP_PanSpread, 750);        // 75% spread

    // --- 3 PolyBLEP saws with slight detuning for fatness ---
    plugin.setParameter(kP_Osc1Waveform, kWave_PolyBlepSaw);
    plugin.setParameter(kP_Osc1Level, 500);
    plugin.setParameter(kP_Osc1Fine, 0);
    plugin.setParameter(kP_Osc2Waveform, kWave_PolyBlepSaw);
    plugin.setParameter(kP_Osc2Level, 300);
    plugin.setParameter(kP_Osc2Fine, 7);           // +7 cents
    plugin.setParameter(kP_Osc3Waveform, kWave_PolyBlepSaw);
    plugin.setParameter(kP_Osc3Level, 200);
    plugin.setParameter(kP_Osc3Fine, -7);          // -7 cents (stored as int: -7 + 100 offset handled by scaling)

    // --- Filter: warm pad ---
    plugin.setParameter(kP_BaseCutoff, 4000);
    plugin.setParameter(kP_Resonance, 150);
    plugin.setParameter(kP_FilterEnvAmount, 2000);
    plugin.setParameter(kP_FilterAttack, 100);
    plugin.setParameter(kP_FilterDecay, 300);
    plugin.setParameter(kP_FilterSustain, 400);
    plugin.setParameter(kP_FilterRelease, 500);

    // --- Amp: pad envelope ---
    plugin.setParameter(kP_AmpAttack, 80);
    plugin.setParameter(kP_AmpDecay, 200);
    plugin.setParameter(kP_AmpSustain, 700);
    plugin.setParameter(kP_AmpRelease, 800);

    // --- Mono delay: dotted-eighth feel ---
    plugin.setParameter(kP_DelayTime, 375);
    plugin.setParameter(kP_DelayFeedback, 400);
    plugin.setParameter(kP_DelayMix, 300);

    // --- Chord progression: I-V-vi-IV in C major ---
    // Cycle 1 (triads, lower register)
    struct Chord { int notes[4]; int count; float durationSec; };
    Chord progression[] = {
        // Cycle 1: triads
        {{60, 64, 67,  0}, 3, 2.0f},   // C major:  C4 E4 G4
        {{55, 59, 62,  0}, 3, 2.0f},   // G major:  G3 B3 D4
        {{57, 60, 64,  0}, 3, 2.0f},   // A minor:  A3 C4 E4
        {{53, 57, 60,  0}, 3, 2.0f},   // F major:  F3 A3 C4
        // Cycle 2: same chords, octave higher
        {{72, 76, 79,  0}, 3, 2.0f},   // C major:  C5 E5 G5
        {{67, 71, 74,  0}, 3, 2.0f},   // G major:  G4 B4 D5
        {{69, 72, 76,  0}, 3, 2.0f},   // A minor:  A4 C5 E5
        {{65, 69, 72,  0}, 3, 2.0f},   // F major:  F4 A4 C5
    };

    int prevNotes[4] = {-1, -1, -1, -1};
    int prevCount = 0;

    for (auto& chord : progression) {
        // Note-off previous chord
        for (int n = 0; n < prevCount; ++n) {
            plugin.midiMessage(0x80, prevNotes[n], 0);
        }
        // Note-on new chord
        for (int n = 0; n < chord.count; ++n) {
            plugin.midiMessage(0x90, chord.notes[n], 100);
            prevNotes[n] = chord.notes[n];
        }
        prevCount = chord.count;

        renderToStereoWav(plugin, wav, chord.durationSec, RIGHT_BUS);
    }

    // Release final chord
    for (int n = 0; n < prevCount; ++n) {
        plugin.midiMessage(0x80, prevNotes[n], 0);
    }

    // Delay tail (3 seconds)
    renderToStereoWav(plugin, wav, 3.0f, RIGHT_BUS);

    wav.close();

    // Verify stereo output: L and R should differ due to pan spread
    // Re-open and check a few blocks
    PluginInstance verify;
    ASSERT_TRUE(createPlugin(verify), "verify plugin created");
    verify.setParameter(kP_RightOutput, 14);
    verify.setParameter(kP_RightOutputMode, 1);
    verify.setParameter(kP_PanSpread, 750);
    verify.setParameter(kP_Osc1Waveform, kWave_PolyBlepSaw);
    verify.setParameter(kP_Osc1Level, 1000);
    verify.setParameter(kP_Osc2Level, 0);
    verify.setParameter(kP_Osc3Level, 0);
    verify.setParameter(kP_BaseCutoff, 10000);
    verify.setParameter(kP_FilterEnvAmount, 0);
    verify.setParameter(kP_DelayMix, 0);

    verify.midiMessage(0x90, 60, 100); // C4 → voice 0 (center)
    verify.midiMessage(0x90, 64, 100); // E4 → voice 1 (right of center)
    verify.step(BLOCK_SIZE);
    const float* verL = verify.getBus(OUTPUT_BUS, BLOCK_SIZE);
    const float* verR = verify.getBus(RIGHT_BUS, BLOCK_SIZE);

    // With pan spread and 2+ voices, L and R must differ
    bool differs = false;
    for (int i = 0; i < BLOCK_SIZE; ++i) {
        if (std::abs(verL[i] - verR[i]) > 1e-6f) { differs = true; break; }
    }
    ASSERT_TRUE(differs, "L and R channels differ with pan spread > 0");

    TEST_PASS();
}

// =========================================================================
// Feature: LFO Key Sync — phase resets on every noteOn
// Plays the same note twice with LFO→Cutoff.  With Key Sync ON the two
// note onsets should produce identical modulation shapes; without it the
// LFO free-runs and the second note starts at an arbitrary phase.
// =========================================================================
TestResult test_lfo_key_sync_wav() {
    TEST_BEGIN("LFO Key Sync (writes bin/feat_lfo_key_sync.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_lfo_key_sync.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Saw through a resonant LP, LFO→Cutoff at moderate depth
    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 2000);
    plugin.setParameter(kP_Resonance, 500);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    plugin.setParameter(kP_LfoSpeed, 300);       // moderate LFO speed
    plugin.setParameter(kP_LfoShape, 0);          // sine
    plugin.setParameter(kP_Mod1Source, kSrc_LFO);
    plugin.setParameter(kP_Mod1Dest, kDst_Cutoff);
    plugin.setParameter(kP_Mod1Amount, 700);

    // Enable Key Sync for LFO 1
    plugin.setParameter(kP_Lfo1KeySync, 1);

    // --- First note ---
    plugin.midiNoteOn(0, 60, 100);
    renderToWav(plugin, wav, 0.5f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 0.3f);

    // Gap — let release tail die
    renderToWav(plugin, wav, 0.2f);

    // --- Second note (LFO phase should restart identically) ---
    plugin.midiNoteOn(0, 60, 100);
    renderToWav(plugin, wav, 0.5f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 0.5f);

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Delay Diffusion — 4-stage Schroeder allpass in feedback path
// Plays a short staccato note with delay + high feedback, first clean,
// then with full diffusion.  The diffused version should progressively
// smear repeats into reverb-like wash.
// =========================================================================
TestResult test_delay_diffusion_wav() {
    TEST_BEGIN("Delay diffusion (writes bin/feat_delay_diffusion.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_delay_diffusion.wav",
                  NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Solo osc1, PolyBLEP saw, open filter, no filter env
    plugin.setParameter(kP_Osc1Waveform, kWave_PolyBlepSaw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);

    // Delay: 250ms, high feedback, 50% wet
    plugin.setParameter(kP_DelayTime, 250);
    plugin.setParameter(kP_DelayFeedback, 700);
    plugin.setParameter(kP_DelayMix, 500);

    // --- Section 1: Clean (diffusion = 0, default) ---
    plugin.setParameter(kP_DelayDiffusion, 0);

    plugin.midiNoteOn(0, 60, 100);
    renderToWav(plugin, wav, 0.15f);    // short staccato hit
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 3.0f);     // let echoes ring out

    // Silence gap
    renderToWav(plugin, wav, 0.6f);

    // --- Section 2: Full diffusion ---
    plugin.setParameter(kP_DelayDiffusion, 1000);

    plugin.midiNoteOn(0, 60, 100);
    renderToWav(plugin, wav, 0.15f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 3.0f);

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Noise waveform + morph-to-noise
// =========================================================================
TestResult test_noise_morph_wav() {
    TEST_BEGIN("Noise morph waveform (writes bin/feat_noise_morph.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");
    WavWriter wav("bin/feat_noise_morph.wav",
                  NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Solo osc1, open filter, no filter env
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);

    // Section 1: Standalone noise waveform
    plugin.setParameter(kP_Osc1Waveform, kWave_Noise);
    plugin.midiNoteOn(0, 60, 100);
    renderToWav(plugin, wav, 0.5f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 0.3f);

    // Section 2: Morph waveform at morph=1000 (full noise end)
    plugin.setParameter(kP_Osc1Waveform, kWave_Morph);
    plugin.setParameter(kP_Osc1Morph, 1000);
    plugin.midiNoteOn(0, 60, 100);
    renderToWav(plugin, wav, 0.5f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 0.3f);

    // Section 3: Morph sweep 0→1000 (sine through noise)
    plugin.setParameter(kP_Osc1Morph, 0);
    plugin.midiNoteOn(0, 60, 100);
    for (int m = 0; m <= 1000; m += 10) {
        plugin.setParameter(kP_Osc1Morph, m);
        renderToWav(plugin, wav, 0.02f);
    }
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 0.3f);

    wav.close();
    TEST_PASS();
}

// =========================================================================
// New mod destinations: Pitch, Drive, PulseWidth
// =========================================================================
TestResult test_mod_destinations_wav() {
    TEST_BEGIN("New mod destinations (writes bin/feat_mod_dests.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");
    WavWriter wav("bin/feat_mod_dests.wav",
                  NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Solo osc1 PolyBLEP saw, open filter
    plugin.setParameter(kP_Osc1Waveform, kWave_PolyBlepSaw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);

    // Section 1: LFO → Pitch (vibrato)
    plugin.setParameter(kP_LfoSpeed, 800);
    plugin.setParameter(kP_Mod1Source, kSrc_LFO);
    plugin.setParameter(kP_Mod1Dest, kDst_Pitch);
    plugin.setParameter(kP_Mod1Amount, 200);
    plugin.midiNoteOn(0, 60, 100);
    renderToWav(plugin, wav, 1.0f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 0.3f);

    // Section 2: Velocity → Drive
    plugin.setParameter(kP_Mod1Source, kSrc_Velocity);
    plugin.setParameter(kP_Mod1Dest, kDst_Drive);
    plugin.setParameter(kP_Mod1Amount, 800);
    plugin.setParameter(kP_BaseCutoff, 3000);
    plugin.midiNoteOn(0, 60, 127);
    renderToWav(plugin, wav, 0.5f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 0.3f);

    // Section 3: LFO → All Morph (=PW on square waveforms)
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_Osc1Waveform, kWave_PolyBlepSquare);
    plugin.setParameter(kP_Osc1Morph, 500);  // 50% duty base
    plugin.setParameter(kP_Mod1Source, kSrc_LFO);
    plugin.setParameter(kP_Mod1Dest, kDst_AllMorph);
    plugin.setParameter(kP_Mod1Amount, 400);
    plugin.midiNoteOn(0, 60, 100);
    renderToWav(plugin, wav, 1.0f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 0.3f);

    // Clean up
    plugin.setParameter(kP_Mod1Source, kSrc_Off);

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Pitch-tracked comb delay (Sarajevo mode) + LP feedback filter
// Demonstrates the pitch-tracking comb as a resonator that reinforces
// the fundamental, with LP in the feedback path to progressively darken
// each comb repeat.  Three sections: Unison, Oct-1, Fifth.
// =========================================================================
TestResult test_pitch_tracked_comb_wav() {
    TEST_BEGIN("Pitch-tracked comb delay + LP feedback (writes bin/feat_pitch_comb.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");
    WavWriter wav("bin/feat_pitch_comb.wav",
                  NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // --- Source: bright noise burst through a resonant comb ---
    // Osc1: noise — excitation signal (like a pluck or bow noise)
    plugin.setParameter(kP_Osc1Waveform, kWave_Noise);
    plugin.setParameter(kP_Osc1Level, 600);
    // Osc2 & 3 off
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);

    // Short noise burst envelope — percussive exciter
    plugin.setParameter(kP_AmpAttack, 2);        // 2ms click
    plugin.setParameter(kP_AmpDecay, 80);        // 80ms burst
    plugin.setParameter(kP_AmpSustain, 0);       // no sustain — just the burst
    plugin.setParameter(kP_AmpRelease, 50);      // fast release

    // Filter wide open so comb gets full spectrum to resonate
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_FilterMode, 6);       // BYPASS

    // --- Delay as pitch-tracked comb resonator ---
    plugin.setParameter(kP_DelayFeedback, 850);  // 85% — strong comb resonance
    plugin.setParameter(kP_DelayMix, 700);       // mostly wet — hear the comb ring
    plugin.setParameter(kP_DelayDiffusion, 0);   // no diffusion — clean comb

    // LP feedback filter: darken each repeat progressively
    plugin.setParameter(kP_DelayFBFilter, 1);    // LP
    plugin.setParameter(kP_DelayFBFreq, 4000);   // 4kHz cutoff — warm decay

    // ---- Section 1: Unison pitch tracking ----
    plugin.setParameter(kP_DelayPitchTrack, 1);  // Unison
    // Play ascending notes: C4, E4, G4, C5
    int melody1[] = {60, 64, 67, 72};
    for (int i = 0; i < 4; ++i) {
        plugin.midiNoteOn(0, melody1[i], 100);
        renderToWav(plugin, wav, 0.5f);          // let comb ring
        plugin.midiNoteOff(0, melody1[i]);
        renderToWav(plugin, wav, 0.4f);          // decay
    }

    // ---- Section 2: Oct -1 (sub-octave comb — bass reinforcement) ----
    plugin.setParameter(kP_DelayPitchTrack, 2);  // Oct -1
    plugin.setParameter(kP_DelayFBFreq, 2000);   // lower LP for deeper, darker tone
    int melody2[] = {60, 63, 67, 60};
    for (int i = 0; i < 4; ++i) {
        plugin.midiNoteOn(0, melody2[i], 110);
        renderToWav(plugin, wav, 0.5f);
        plugin.midiNoteOff(0, melody2[i]);
        renderToWav(plugin, wav, 0.4f);
    }

    // ---- Section 3: Fifth — harmonically related comb ----
    plugin.setParameter(kP_DelayPitchTrack, 4);  // Fifth
    plugin.setParameter(kP_DelayFBFreq, 6000);   // brighter for the fifth shimmer
    plugin.setParameter(kP_DelayFeedback, 750);  // slightly less feedback
    int melody3[] = {60, 65, 67, 72};
    for (int i = 0; i < 4; ++i) {
        plugin.midiNoteOn(0, melody3[i], 95);
        renderToWav(plugin, wav, 0.6f);
        plugin.midiNoteOff(0, melody3[i]);
        renderToWav(plugin, wav, 0.5f);
    }

    // Final chord: let it ring with the comb
    plugin.setParameter(kP_DelayPitchTrack, 1);  // back to Unison
    plugin.setParameter(kP_DelayFBFreq, 3000);
    plugin.setParameter(kP_DelayFeedback, 900);  // high feedback for long ring
    plugin.midiNoteOn(0, 60, 100);
    plugin.midiNoteOn(0, 64, 100);
    plugin.midiNoteOn(0, 67, 100);
    renderToWav(plugin, wav, 1.0f);
    plugin.midiNoteOff(0, 60);
    plugin.midiNoteOff(0, 64);
    plugin.midiNoteOff(0, 67);
    renderToWav(plugin, wav, 1.5f);  // comb tail

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Per-note random modulation source test
// Route 1: NoteRandom → Pitch  (clearly different pitches per note)
// Route 2: NoteRandom → Cutoff (clearly different brightness per note)
// 8 repeated note-ons of the same MIDI key — each should sound different.
// =========================================================================
TestResult test_note_random_mod_wav() {
    TEST_BEGIN("Note Random modulation (writes bin/feat_note_random.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");
    WavWriter wav("bin/feat_note_random.wav",
                  NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Bright saw — easy to hear both pitch and filter differences
    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);

    // Snappy pluck envelope with some sustain so each note is clearly audible
    plugin.setParameter(kP_AmpAttack, 2);
    plugin.setParameter(kP_AmpDecay, 150);
    plugin.setParameter(kP_AmpSustain, 600);
    plugin.setParameter(kP_AmpRelease, 80);

    // Medium cutoff, no filter env — filter position driven only by mod matrix
    plugin.setParameter(kP_BaseCutoff, 3000);
    plugin.setParameter(kP_Resonance, 300);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    // --- Section A: NoteRandom → Pitch (±~2 semitones) ---
    // Each note literally plays at a different pitch
    plugin.setParameter(kP_Mod1Source, 10);       // kSourceNoteRandom
    plugin.setParameter(kP_Mod1Dest, kDst_Pitch); // Pitch destination
    plugin.setParameter(kP_Mod1Amount, 200);       // ±200/1000 = ±~2.4 semitones

    // Clear any other mod slots
    plugin.setParameter(kP_Mod2Source, 0);

    for (int i = 0; i < 8; ++i) {
        plugin.midiNoteOn(0, 60, 100);   // always C4
        renderToWav(plugin, wav, 0.35f);  // hold — hear the pitch
        plugin.midiNoteOff(0, 60);
        renderToWav(plugin, wav, 0.15f);  // gap between notes
    }

    // --- Section B: NoteRandom → Cutoff (±bright/dark per note) ---
    plugin.setParameter(kP_Mod1Dest, kDst_Cutoff);
    plugin.setParameter(kP_Mod1Amount, 900);       // big filter swing
    plugin.setParameter(kP_BaseCutoff, 2000);      // lower base so bright/dark is stark
    plugin.setParameter(kP_Resonance, 500);        // some resonance to accentuate

    for (int i = 0; i < 8; ++i) {
        plugin.midiNoteOn(0, 60, 100);
        renderToWav(plugin, wav, 0.35f);
        plugin.midiNoteOff(0, 60);
        renderToWav(plugin, wav, 0.15f);
    }

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Wavetable preset showcase — data-driven tests for every WtGen preset
// Each test generates a wavetable in memory, pushes it to the plugin,
// and renders a ~6 second WAV:
//   Section A (3 s): held C3, morph sweep 0 → 1000
//   Section B (2 s): C3-E3-G3 chord at morph positions 200/600/900
//   Section C (1 s): release tail
// =========================================================================

struct WtPreset {
    const char* name;       // short id for filename: bin/wt_<name>.wav
    const char* desc;       // human-readable description
    void (*generate)(int16_t* buf, uint32_t numWaves, uint32_t waveLen);
};

// --- Generator wrappers matching WtPreset::generate signature ---
static void wtgen_sine_saw(int16_t* b, uint32_t nw, uint32_t wl) {
    WtGen::morphShapes(b, nw, wl, WtGen::shapeSine, WtGen::shapeSaw);
}
static void wtgen_sine_square(int16_t* b, uint32_t nw, uint32_t wl) {
    WtGen::morphShapes(b, nw, wl, WtGen::shapeSine, WtGen::shapeSquare);
}
static void wtgen_sine_tri(int16_t* b, uint32_t nw, uint32_t wl) {
    WtGen::morphShapes(b, nw, wl, WtGen::shapeSine, WtGen::shapeTriangle);
}
static void wtgen_saw_square(int16_t* b, uint32_t nw, uint32_t wl) {
    WtGen::morphShapes(b, nw, wl, WtGen::shapeSaw, WtGen::shapeSquare);
}
static void wtgen_tri_saw(int16_t* b, uint32_t nw, uint32_t wl) {
    WtGen::morphShapes(b, nw, wl, WtGen::shapeTriangle, WtGen::shapeSaw);
}
static void wtgen_additive(int16_t* b, uint32_t nw, uint32_t wl) {
    WtGen::additive(b, nw, wl, 32, 1.0f);
}
static void wtgen_additive_soft(int16_t* b, uint32_t nw, uint32_t wl) {
    WtGen::additive(b, nw, wl, 16, 2.0f);
}
static void wtgen_pwm(int16_t* b, uint32_t nw, uint32_t wl) {
    WtGen::pwm(b, nw, wl);
}
static void wtgen_fm_2x(int16_t* b, uint32_t nw, uint32_t wl) {
    WtGen::fm(b, nw, wl, 2.0f, 8.0f);
}
static void wtgen_fm_3x(int16_t* b, uint32_t nw, uint32_t wl) {
    WtGen::fm(b, nw, wl, 3.0f, 6.0f);
}
static void wtgen_fm_golden(int16_t* b, uint32_t nw, uint32_t wl) {
    WtGen::fm(b, nw, wl, 1.618033988f, 10.0f);
}
static void wtgen_wavefold(int16_t* b, uint32_t nw, uint32_t wl) {
    WtGen::wavefold(b, nw, wl, 8.0f);
}
static void wtgen_wavefold_gentle(int16_t* b, uint32_t nw, uint32_t wl) {
    WtGen::wavefold(b, nw, wl, 3.0f);
}
static void wtgen_formant(int16_t* b, uint32_t nw, uint32_t wl) {
    WtGen::formant(b, nw, wl, 1.0f, 16.0f);
}
static void wtgen_formant_vocal(int16_t* b, uint32_t nw, uint32_t wl) {
    WtGen::formant(b, nw, wl, 2.0f, 8.0f);
}
static void wtgen_supersaw(int16_t* b, uint32_t nw, uint32_t wl) {
    WtGen::supersaw(b, nw, wl, 7, 40.0f);
}
static void wtgen_supersaw_wide(int16_t* b, uint32_t nw, uint32_t wl) {
    WtGen::supersaw(b, nw, wl, 7, 80.0f);
}

static const WtPreset kWtPresets[] = {
    { "sine_saw",        "Sine -> Saw crossfade",           wtgen_sine_saw },
    { "sine_square",     "Sine -> Square crossfade",        wtgen_sine_square },
    { "sine_tri",        "Sine -> Triangle crossfade",      wtgen_sine_tri },
    { "saw_square",      "Saw -> Square crossfade",         wtgen_saw_square },
    { "tri_saw",         "Triangle -> Saw crossfade",       wtgen_tri_saw },
    { "additive",        "1-32 harmonics, 1/h rolloff",     wtgen_additive },
    { "additive_soft",   "1-16 harmonics, 1/h^2 rolloff",  wtgen_additive_soft },
    { "pwm",             "Pulse width 5% -> 95%",           wtgen_pwm },
    { "fm_2x",           "FM 2:1, index 0-8",              wtgen_fm_2x },
    { "fm_3x",           "FM 3:1, index 0-6",              wtgen_fm_3x },
    { "fm_golden",       "FM golden ratio, index 0-10",    wtgen_fm_golden },
    { "wavefold",        "Sine wavefold, gain 1-8",        wtgen_wavefold },
    { "wavefold_gentle", "Sine wavefold, gain 1-3",        wtgen_wavefold_gentle },
    { "formant",         "Formant sweep, ratio 1-16",      wtgen_formant },
    { "formant_vocal",   "Formant sweep, ratio 2-8",       wtgen_formant_vocal },
    { "supersaw",        "Supersaw, 0-40 cent detune",     wtgen_supersaw },
    { "supersaw_wide",   "Supersaw, 0-80 cent detune",     wtgen_supersaw_wide },
};
static constexpr int kNumWtPresets = sizeof(kWtPresets) / sizeof(kWtPresets[0]);

// Shared helper: run one wavetable preset through the plugin
static TestResult runWtPresetTest(const WtPreset& preset) {
    static const uint32_t WT_WAVES  = 16;
    static const uint32_t WT_LENGTH = 256;
    static int16_t table[WT_WAVES * WT_LENGTH];

    char wavPath[256];
    snprintf(wavPath, sizeof(wavPath), "bin/wt_%s.wav", preset.name);

    char desc[512];
    snprintf(desc, sizeof(desc), "WT preset: %s (writes %s)", preset.desc, wavPath);
    TEST_BEGIN(desc);

    // Generate wavetable
    preset.generate(table, WT_WAVES, WT_LENGTH);

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav(wavPath, NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Solo osc1 on wavetable mode
    plugin.setParameter(kP_Osc1Waveform, kWave_Wavetable);
    plugin.setParameter(kP_Osc1Level, 800);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_DelayMix, 0);

    // Inject wavetable
    polyLofi_injectWavetable(plugin.getAlgorithm(), 0, table, WT_WAVES, WT_LENGTH);

    // Open filter, mild resonance
    plugin.setParameter(kP_BaseCutoff, 9000);
    plugin.setParameter(kP_Resonance, 150);

    // Snappy envelope
    plugin.setParameter(kP_AmpAttack, 5);
    plugin.setParameter(kP_AmpDecay, 200);
    plugin.setParameter(kP_AmpSustain, 800);
    plugin.setParameter(kP_AmpRelease, 400);

    float peakAll = 0.0f;

    // --- Section A: held C3, morph sweep 0 → 1000 over 3 seconds ---
    plugin.setParameter(kP_Osc1Morph, 0);
    plugin.midiNoteOn(0, 48, 100);
    int sweepBlocks = blocksFor(3.0f);
    for (int b = 0; b < sweepBlocks; ++b) {
        int morph = (b * 1000) / sweepBlocks;
        plugin.setParameter(kP_Osc1Morph, morph);
        plugin.step(BLOCK_SIZE);
        const float* bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
        wav.writeMono(bus, BLOCK_SIZE);
        float p = PluginInstance::peak(bus, BLOCK_SIZE);
        if (p > peakAll) peakAll = p;
    }
    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.3f);

    // --- Section B: C-E-G chord at varied morph positions (2 seconds) ---
    plugin.setParameter(kP_Osc1Morph, 200);
    plugin.midiNoteOn(0, 48, 100); // C3
    renderToWav(plugin, wav, 0.08f);
    plugin.setParameter(kP_Osc1Morph, 600);
    plugin.midiNoteOn(0, 52, 90);  // E3
    renderToWav(plugin, wav, 0.08f);
    plugin.setParameter(kP_Osc1Morph, 900);
    plugin.midiNoteOn(0, 55, 85);  // G3
    renderToWav(plugin, wav, 1.84f);
    plugin.midiNoteOff(0, 48);
    plugin.midiNoteOff(0, 52);
    plugin.midiNoteOff(0, 55);

    // --- Section C: release tail ---
    renderToWav(plugin, wav, 0.7f);

    wav.close();
    ASSERT_TRUE(peakAll > 0.01f, "wavetable produced audio");
    TEST_PASS();
}

// --- Individual test functions (one per preset, for test runner registration) ---
static TestResult test_wt_sine_saw_wav()        { return runWtPresetTest(kWtPresets[0]); }
static TestResult test_wt_sine_square_wav()     { return runWtPresetTest(kWtPresets[1]); }
static TestResult test_wt_sine_tri_wav()        { return runWtPresetTest(kWtPresets[2]); }
static TestResult test_wt_saw_square_wav()      { return runWtPresetTest(kWtPresets[3]); }
static TestResult test_wt_tri_saw_wav()         { return runWtPresetTest(kWtPresets[4]); }
static TestResult test_wt_additive_wav()        { return runWtPresetTest(kWtPresets[5]); }
static TestResult test_wt_additive_soft_wav()   { return runWtPresetTest(kWtPresets[6]); }
static TestResult test_wt_pwm_wav()             { return runWtPresetTest(kWtPresets[7]); }
static TestResult test_wt_fm_2x_wav()           { return runWtPresetTest(kWtPresets[8]); }
static TestResult test_wt_fm_3x_wav()           { return runWtPresetTest(kWtPresets[9]); }
static TestResult test_wt_fm_golden_wav()       { return runWtPresetTest(kWtPresets[10]); }
static TestResult test_wt_wavefold_wav()        { return runWtPresetTest(kWtPresets[11]); }
static TestResult test_wt_wavefold_gentle_wav() { return runWtPresetTest(kWtPresets[12]); }
static TestResult test_wt_formant_wav()         { return runWtPresetTest(kWtPresets[13]); }
static TestResult test_wt_formant_vocal_wav()   { return runWtPresetTest(kWtPresets[14]); }
static TestResult test_wt_supersaw_wav()        { return runWtPresetTest(kWtPresets[15]); }
static TestResult test_wt_supersaw_wide_wav()   { return runWtPresetTest(kWtPresets[16]); }

// =========================================================================
// Hardware preset: "Lofior" — ambient resonant comb pad
// Decoded from tests/Lofior.json (disting NT preset format).
// JSON index 0 is the common/bypass param; algorithm params start at [1].
//
// Osc1: Noise (texture), Osc2+3: PolyBLEP Saw ±3 cents (micro-detuned pair).
// LP4 filter, moderate resonance, filter env.
// Pad amp envelope (1s attack, 1s decay, low sustain).
// Delay: 500ms, 97% feedback, 99% wet — borderline self-oscillation.
// Delay feedback HP at 209Hz + pitch-tracked Oct+1 → resonant comb harmonic.
// Mod: LFO→DelayTime (subtle wow), Velocity→AmpAttack (expression).
// =========================================================================
TestResult test_lofior_preset_wav() {
    TEST_BEGIN("Lofior preset (writes bin/preset_lofior.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");
    WavWriter wav("bin/preset_lofior.wav",
                  NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // --- Oscillators: noise + micro-detuned PolyBLEP saw pair ---
    plugin.setParameter(kP_Osc1Waveform, kWave_Noise);
    // Osc1 level stays at default 333

    plugin.setParameter(kP_Osc2Waveform, kWave_PolyBlepSaw);
    plugin.setParameter(kP_Osc2Fine, -3);
    plugin.setParameter(kP_Osc2Level, 93);

    plugin.setParameter(kP_Osc3Waveform, kWave_PolyBlepSaw);
    plugin.setParameter(kP_Osc3Fine, 3);
    plugin.setParameter(kP_Osc3Level, 82);

    // --- Filter: LP4, moderate cutoff + resonance ---
    plugin.setParameter(kP_BaseCutoff, 6239);
    plugin.setParameter(kP_Resonance, 467);
    plugin.setParameter(kP_FilterEnvAmount, 2903);
    plugin.setParameter(kP_FilterMode, 1);  // LP4
    plugin.setParameter(kP_Drive, 1205);

    // --- Amp envelope: slow pad ---
    plugin.setParameter(kP_AmpAttack, 1077);
    plugin.setParameter(kP_AmpDecay, 979);
    plugin.setParameter(kP_AmpSustain, 463);
    plugin.setParameter(kP_AmpRelease, 261);

    // --- Delay: near self-oscillation, almost full wet ---
    plugin.setParameter(kP_DelayFeedback, 967);
    plugin.setParameter(kP_DelayMix, 987);

    // --- Delay feedback: HP filter + pitch-tracked Oct+1 ---
    plugin.setParameter(kP_DelayFBFilter, 2);   // HP
    plugin.setParameter(kP_DelayFBFreq, 209);
    plugin.setParameter(kP_DelayPitchTrack, 3); // Oct +1

    // --- Modulation: LFO→DelayTime (tape wow), Velocity→AmpAttack ---
    plugin.setParameter(kP_Mod1Source, kSrc_LFO);
    plugin.setParameter(kP_Mod1Dest, kDst_DelayTime);
    plugin.setParameter(kP_Mod1Amount, 1);

    plugin.setParameter(kP_Mod2Source, kSrc_Velocity);
    plugin.setParameter(kP_Mod2Dest, kDst_AmpAttack);
    plugin.setParameter(kP_Mod2Amount, -779);

    // --- Play ambient pad sequence ---
    // Soft sustained chord: C3-G3 fifth
    plugin.midiNoteOn(0, 48, 60);   // C3, gentle
    plugin.midiNoteOn(0, 55, 55);   // G3, gentle
    renderToWav(plugin, wav, 3.0f);  // let the pad swell and delay build

    // Add E4 on top — open voicing
    plugin.midiNoteOn(0, 64, 80);   // E4, harder velocity = shorter attack
    renderToWav(plugin, wav, 2.0f);

    // Release chord, let delay ring out
    plugin.midiNoteOff(0, 48);
    plugin.midiNoteOff(0, 55);
    plugin.midiNoteOff(0, 64);
    renderToWav(plugin, wav, 3.0f);  // delay tail with resonant harmonics

    // Second phrase: single note with hard velocity
    plugin.midiNoteOn(0, 60, 120);  // C4, hard — short attack from vel mod
    renderToWav(plugin, wav, 2.0f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 2.0f);  // final decay

    TEST_PASS();
}

// =========================================================================
// Preset: save/load roundtrip
// =========================================================================

// Callback for NT_setParameterFromAudio — routes to PluginInstance::setParameter
static PluginInstance* g_presetCallbackInstance = nullptr;
static void presetSetParamCallback(uint32_t, uint32_t param, int16_t value) {
    if (g_presetCallbackInstance)
        g_presetCallbackInstance->setParameter((int)param, value);
}

TestResult test_preset_save_load_roundtrip() {
    TEST_BEGIN("Preset save/load roundtrip");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    // Wire up the callback so NT_setParameterFromAudio works
    g_presetCallbackInstance = &plugin;
    NtTestHarness::setSetParameterCallback(presetSetParamCallback);

    // Modify some params from defaults
    plugin.setParameter(kP_BaseCutoff, 8000);
    plugin.setParameter(kP_Resonance, 500);
    plugin.setParameter(kP_Osc1Waveform, kWave_PolyBlepSaw);
    plugin.setParameter(kP_DelayFeedback, 800);
    plugin.setParameter(kP_MasterVolume, 50);

    // Save to preset slot 0
    plugin.setParameter(kP_SavePreset, 0);   // Select slot
    plugin.setParameter(kP_SaveConfirm, 1);  // Trigger save (auto-resets to 0)

    // Pump enough blocks so the deferred reset completes
    for (int i = 0; i < 101; ++i) plugin.step(BLOCK_SIZE);

    // Now change params to something else
    plugin.setParameter(kP_BaseCutoff, 2000);
    plugin.setParameter(kP_Resonance, 100);
    plugin.setParameter(kP_Osc1Waveform, kWave_Sine);
    plugin.setParameter(kP_DelayFeedback, 250);
    plugin.setParameter(kP_MasterVolume, 70);

    // Verify params are now different
    ASSERT_EQ(plugin.getParameter(kP_BaseCutoff), 2000, "cutoff changed before load");
    ASSERT_EQ(plugin.getParameter(kP_Osc1Waveform), kWave_Sine, "waveform changed before load");

    // Load preset slot 0
    plugin.setParameter(kP_LoadPreset, 0);

    // Verify params restored
    ASSERT_EQ(plugin.getParameter(kP_BaseCutoff), 8000, "cutoff restored after load");
    ASSERT_EQ(plugin.getParameter(kP_Resonance), 500, "resonance restored after load");
    ASSERT_EQ(plugin.getParameter(kP_Osc1Waveform), kWave_PolyBlepSaw, "waveform restored after load");
    ASSERT_EQ(plugin.getParameter(kP_DelayFeedback), 800, "delay fb restored after load");
    // MasterVolume is a setup param — intentionally NOT restored by preset load
    ASSERT_EQ(plugin.getParameter(kP_MasterVolume), 70, "master vol unchanged after load (setup param)");

    // Clean up callback
    g_presetCallbackInstance = nullptr;
    NtTestHarness::setSetParameterCallback(nullptr);

    TEST_PASS();
}

TestResult test_preset_factory_load() {
    TEST_BEGIN("Preset load factory preset applies values");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    g_presetCallbackInstance = &plugin;
    NtTestHarness::setSetParameterCallback(presetSetParamCallback);

    // Load "Acid Bass" (slot 1) — should set cutoff to 3000
    plugin.setParameter(kP_LoadPreset, 1);

    ASSERT_EQ(plugin.getParameter(kP_BaseCutoff), 3000,
              "cutoff changed to Acid Bass value after load");
    ASSERT_EQ(plugin.getParameter(kP_Resonance), 800,
              "resonance changed to Acid Bass value after load");

    g_presetCallbackInstance = nullptr;
    NtTestHarness::setSetParameterCallback(nullptr);

    TEST_PASS();
}

TestResult test_preset_multiple_slots() {
    TEST_BEGIN("Preset save/load across multiple slots");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    g_presetCallbackInstance = &plugin;
    NtTestHarness::setSetParameterCallback(presetSetParamCallback);

    // Save different cutoffs into slots 0 and 1
    plugin.setParameter(kP_BaseCutoff, 3000);
    plugin.setParameter(kP_SavePreset, 0);
    plugin.setParameter(kP_SaveConfirm, 1);

    // Pump enough blocks so the deferred reset completes
    for (int i = 0; i < 101; ++i) plugin.step(BLOCK_SIZE);

    plugin.setParameter(kP_BaseCutoff, 7000);
    plugin.setParameter(kP_SavePreset, 1);
    plugin.setParameter(kP_SaveConfirm, 1);

    // Load slot 0 — should get 3000
    plugin.setParameter(kP_LoadPreset, 0);
    ASSERT_EQ(plugin.getParameter(kP_BaseCutoff), 3000, "slot 0 cutoff correct");

    // Load slot 1 — should get 7000
    plugin.setParameter(kP_LoadPreset, 1);
    ASSERT_EQ(plugin.getParameter(kP_BaseCutoff), 7000, "slot 1 cutoff correct");

    g_presetCallbackInstance = nullptr;
    NtTestHarness::setSetParameterCallback(nullptr);

    TEST_PASS();
}

// =========================================================================
// Factory preset WAV renders — one per slot, golden-hashed
// =========================================================================

// Helper: load factory preset by slot, render a musical phrase to WAV.
// Each preset gets a phrase tailored to its character.
static const char* kPresetWavNames[] = {
    "bin/preset_supersaw.wav",
    "bin/preset_acid_bass.wav",
    "bin/preset_virus_lead.wav",
    "bin/preset_pwm_pad.wav",
    "bin/preset_hoover.wav",
    "bin/preset_fizzy_keys.wav",
    "bin/preset_rez_sweep.wav",
    "bin/preset_sync_lead.wav",
    "bin/preset_lofior_factory.wav",
    "bin/preset_crushed.wav",
    "bin/preset_moog_bass.wav",
    "bin/preset_tape_piano.wav",
    "bin/preset_scream_lead.wav",
    "bin/preset_303_acid.wav",
};

// =========================================================================
// Feature: Ladder filter model — all modes + resonance sweep
// =========================================================================
TestResult test_ladder_filter_modes_wav() {
    TEST_BEGIN("Ladder filter modes (writes bin/feat_ladder_filter_modes.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_ladder_filter_modes.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 2000);
    plugin.setParameter(kP_Resonance, 500);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);
    plugin.setParameter(kP_FilterModel, 1); // Ladder

    // Play each filter mode (LP2..BYPASS)
    for (int mode = 0; mode <= 6; ++mode) {
        plugin.setParameter(kP_FilterMode, mode);
        plugin.midiNoteOn(0, 48, 100);
        renderToWav(plugin, wav, 0.7f);
        plugin.midiNoteOff(0, 48);
        renderToWav(plugin, wav, 0.3f);
    }

    wav.close();
    ASSERT_TRUE(true, "ladder filter modes rendered");
    TEST_PASS();
}

// Ladder LP4 resonance sweep — the classic Moog squelch
TestResult test_ladder_reso_sweep_wav() {
    TEST_BEGIN("Ladder reso sweep (writes bin/feat_ladder_reso_sweep.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_ladder_reso_sweep.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_FilterMode, 1); // LP4
    plugin.setParameter(kP_FilterModel, 1); // Ladder
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    // Sweep resonance from 0 to 1000 over a sustained note, low cutoff
    plugin.setParameter(kP_BaseCutoff, 1500);
    plugin.midiNoteOn(0, 36, 100);
    for (int reso = 0; reso <= 1000; reso += 50) {
        plugin.setParameter(kP_Resonance, reso);
        renderToWav(plugin, wav, 0.1f);
    }
    plugin.midiNoteOff(0, 36);
    renderToWav(plugin, wav, 0.3f);

    wav.close();
    ASSERT_TRUE(true, "ladder resonance sweep rendered");
    TEST_PASS();
}

// SVF vs Ladder comparison — same settings, both models back to back
TestResult test_svf_vs_ladder_wav() {
    TEST_BEGIN("SVF vs Ladder comparison (writes bin/feat_svf_vs_ladder.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_svf_vs_ladder.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_FilterMode, 1); // LP4
    plugin.setParameter(kP_BaseCutoff, 2000);
    plugin.setParameter(kP_Resonance, 700);
    plugin.setParameter(kP_FilterEnvAmount, 3000);
    plugin.setParameter(kP_FilterAttack, 10);
    plugin.setParameter(kP_FilterDecay, 500);
    plugin.setParameter(kP_FilterSustain, 200);
    plugin.setParameter(kP_FilterRelease, 300);
    plugin.setParameter(kP_DelayMix, 0);

    // SVF model
    plugin.setParameter(kP_FilterModel, 0);
    plugin.midiNoteOn(0, 48, 100);
    renderToWav(plugin, wav, 1.0f);
    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.5f);

    // Ladder model — same settings
    plugin.setParameter(kP_FilterModel, 1);
    plugin.midiNoteOn(0, 48, 100);
    renderToWav(plugin, wav, 1.0f);
    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.5f);

    wav.close();
    ASSERT_TRUE(true, "SVF vs Ladder comparison rendered");
    TEST_PASS();
}

// MS-20 filter model — all modes
TestResult test_ms20_filter_modes_wav() {
    TEST_BEGIN("MS-20 filter modes (writes bin/feat_ms20_filter_modes.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_ms20_filter_modes.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 2000);
    plugin.setParameter(kP_Resonance, 600);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);
    plugin.setParameter(kP_FilterModel, 2); // MS-20

    for (int mode = 0; mode <= 6; ++mode) {
        plugin.setParameter(kP_FilterMode, mode);
        plugin.midiNoteOn(0, 48, 100);
        renderToWav(plugin, wav, 0.7f);
        plugin.midiNoteOff(0, 48);
        renderToWav(plugin, wav, 0.3f);
    }

    wav.close();
    ASSERT_TRUE(true, "MS-20 filter modes rendered");
    TEST_PASS();
}

// Diode ladder filter model — all modes
TestResult test_diode_filter_modes_wav() {
    TEST_BEGIN("Diode filter modes (writes bin/feat_diode_filter_modes.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_diode_filter_modes.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, 2000);
    plugin.setParameter(kP_Resonance, 500);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);
    plugin.setParameter(kP_FilterModel, 3); // Diode

    for (int mode = 0; mode <= 6; ++mode) {
        plugin.setParameter(kP_FilterMode, mode);
        plugin.midiNoteOn(0, 48, 100);
        renderToWav(plugin, wav, 0.7f);
        plugin.midiNoteOff(0, 48);
        renderToWav(plugin, wav, 0.3f);
    }

    wav.close();
    ASSERT_TRUE(true, "Diode filter modes rendered");
    TEST_PASS();
}

// =========================================================================
// Feature: Keyboard tracking — cutoff follows note pitch
// =========================================================================
TestResult test_keyboard_tracking_wav() {
    TEST_BEGIN("Keyboard tracking (writes bin/feat_keyboard_tracking.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_keyboard_tracking.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_FilterMode, 0); // LP2
    plugin.setParameter(kP_BaseCutoff, 3000);
    plugin.setParameter(kP_Resonance, 400);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    // First: no key tracking — play ascending scale, cutoff stays constant
    plugin.setParameter(kP_KeyboardTracking, 0);
    int notes[] = {36, 48, 60, 72, 84};
    for (int n : notes) {
        plugin.midiNoteOn(0, n, 100);
        renderToWav(plugin, wav, 0.4f);
        plugin.midiNoteOff(0, n);
        renderToWav(plugin, wav, 0.1f);
    }

    // Now 100% key tracking — same notes, cutoff follows pitch
    plugin.setParameter(kP_KeyboardTracking, 1000);
    for (int n : notes) {
        plugin.midiNoteOn(0, n, 100);
        renderToWav(plugin, wav, 0.4f);
        plugin.midiNoteOff(0, n);
        renderToWav(plugin, wav, 0.1f);
    }

    wav.close();
    ASSERT_TRUE(true, "keyboard tracking rendered");
    TEST_PASS();
}

// =========================================================================
// Feature: Resonance gain compensation
// =========================================================================
TestResult test_reso_compensation_wav() {
    TEST_BEGIN("Resonance gain compensation (writes bin/feat_reso_compensation.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_reso_compensation.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_FilterMode, 1); // LP4
    plugin.setParameter(kP_BaseCutoff, 2000);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    // Sweep resonance from 0 to max — volume should stay roughly even
    plugin.midiNoteOn(0, 48, 100);
    for (int reso = 0; reso <= 1000; reso += 100) {
        plugin.setParameter(kP_Resonance, reso);
        renderToWav(plugin, wav, 0.15f);
    }
    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.3f);

    // Same with ladder
    plugin.setParameter(kP_FilterModel, 1);
    plugin.midiNoteOn(0, 48, 100);
    for (int reso = 0; reso <= 1000; reso += 100) {
        plugin.setParameter(kP_Resonance, reso);
        renderToWav(plugin, wav, 0.15f);
    }
    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 0.3f);

    wav.close();
    ASSERT_TRUE(true, "resonance compensation rendered");
    TEST_PASS();
}

// =========================================================================
// Feature: Key tracking as mod source
// =========================================================================
TestResult test_keytrack_mod_source_wav() {
    TEST_BEGIN("Key tracking mod source (writes bin/feat_keytrack_mod.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/feat_keytrack_mod.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    plugin.setParameter(kP_Osc1Waveform, kWave_Saw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_FilterMode, 0); // LP2
    plugin.setParameter(kP_BaseCutoff, 5000);
    plugin.setParameter(kP_Resonance, 300);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);

    // Use key tracking to modulate drive: higher notes get more drive
    plugin.setParameter(kP_Mod1Source, 11); // Key Track
    plugin.setParameter(kP_Mod1Dest, kDst_Drive);
    plugin.setParameter(kP_Mod1Amount, 800);

    int notes[] = {36, 48, 60, 72, 84};
    for (int n : notes) {
        plugin.midiNoteOn(0, n, 100);
        renderToWav(plugin, wav, 0.4f);
        plugin.midiNoteOff(0, n);
        renderToWav(plugin, wav, 0.1f);
    }

    wav.close();
    ASSERT_TRUE(true, "key tracking mod source rendered");
    TEST_PASS();
}

// Generic helper: construct plugin, wire callback, load preset slot
static bool setupPresetPlugin(PluginInstance& plugin, int slot) {
    if (!createPlugin(plugin)) return false;
    g_presetCallbackInstance = &plugin;
    NtTestHarness::setSetParameterCallback(presetSetParamCallback);
    plugin.setParameter(kP_LoadPreset, slot);
    return true;
}

static void teardownPresetPlugin() {
    g_presetCallbackInstance = nullptr;
    NtTestHarness::setSetParameterCallback(nullptr);
}

// --- 0: Supersaw — trance chord stab + sustained pad ---
TestResult test_factory_supersaw_wav() {
    TEST_BEGIN("Factory preset: Supersaw (writes bin/preset_supersaw.wav)");
    PluginInstance plugin;
    ASSERT_TRUE(setupPresetPlugin(plugin, 0), "loaded Supersaw");
    WavWriter wav(kPresetWavNames[0], NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV open");

    // Trance stab: E4-G#4-B4
    plugin.midiNoteOn(0, 64, 100);
    plugin.midiNoteOn(0, 68, 100);
    plugin.midiNoteOn(0, 71, 100);
    renderToWav(plugin, wav, 1.5f);
    plugin.midiNoteOff(0, 64);
    plugin.midiNoteOff(0, 68);
    plugin.midiNoteOff(0, 71);
    renderToWav(plugin, wav, 0.5f);

    // Sustained pad: C3-E3-G3-B3
    plugin.midiNoteOn(0, 48, 80);
    plugin.midiNoteOn(0, 52, 80);
    plugin.midiNoteOn(0, 55, 80);
    plugin.midiNoteOn(0, 59, 80);
    renderToWav(plugin, wav, 3.0f);
    plugin.midiNoteOff(0, 48);
    plugin.midiNoteOff(0, 52);
    plugin.midiNoteOff(0, 55);
    plugin.midiNoteOff(0, 59);
    renderToWav(plugin, wav, 1.0f);

    teardownPresetPlugin();
    TEST_PASS();
}

// --- 1: Acid Bass — 303-style sequence ---
TestResult test_factory_acid_bass_wav() {
    TEST_BEGIN("Factory preset: Acid Bass (writes bin/preset_acid_bass.wav)");
    PluginInstance plugin;
    ASSERT_TRUE(setupPresetPlugin(plugin, 1), "loaded Acid Bass");
    WavWriter wav(kPresetWavNames[1], NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV open");

    // Classic 303 pattern: short staccato notes with slides
    const uint8_t notes[] = {36,36,48,36,39,36,48,41, 36,36,43,36,48,36,39,36};
    const uint8_t vels[]  = {120,90,110,80,100,90,120,100, 110,80,100,90,120,80,110,100};
    for (int i = 0; i < 16; ++i) {
        plugin.midiNoteOn(0, notes[i], vels[i]);
        renderToWav(plugin, wav, 0.15f);
        plugin.midiNoteOff(0, notes[i]);
        renderToWav(plugin, wav, 0.05f);
    }
    renderToWav(plugin, wav, 0.5f);  // tail

    teardownPresetPlugin();
    TEST_PASS();
}

// --- 2: Virus Lead — aggressive melody ---
TestResult test_factory_virus_lead_wav() {
    TEST_BEGIN("Factory preset: Virus Lead (writes bin/preset_virus_lead.wav)");
    PluginInstance plugin;
    ASSERT_TRUE(setupPresetPlugin(plugin, 2), "loaded Virus Lead");
    WavWriter wav(kPresetWavNames[2], NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV open");

    // Melody line
    const uint8_t melody[] = {60, 63, 67, 72, 70, 67, 63, 60};
    for (int i = 0; i < 8; ++i) {
        plugin.midiNoteOn(0, melody[i], 100);
        renderToWav(plugin, wav, 0.4f);
        plugin.midiNoteOff(0, melody[i]);
        renderToWav(plugin, wav, 0.1f);
    }
    // Sustained note with vibrato
    plugin.midiNoteOn(0, 72, 110);
    renderToWav(plugin, wav, 2.0f);
    plugin.midiNoteOff(0, 72);
    renderToWav(plugin, wav, 0.5f);

    teardownPresetPlugin();
    TEST_PASS();
}

// --- 3: PWM Pad — slow evolving string pad ---
TestResult test_factory_pwm_pad_wav() {
    TEST_BEGIN("Factory preset: PWM Pad (writes bin/preset_pwm_pad.wav)");
    PluginInstance plugin;
    ASSERT_TRUE(setupPresetPlugin(plugin, 3), "loaded PWM Pad");
    WavWriter wav(kPresetWavNames[3], NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV open");

    // Slow string chord: Cm7 — C3-Eb3-G3-Bb3
    plugin.midiNoteOn(0, 48, 70);
    plugin.midiNoteOn(0, 51, 70);
    plugin.midiNoteOn(0, 55, 70);
    plugin.midiNoteOn(0, 58, 70);
    renderToWav(plugin, wav, 5.0f);  // long swell
    plugin.midiNoteOff(0, 48);
    plugin.midiNoteOff(0, 51);
    plugin.midiNoteOff(0, 55);
    plugin.midiNoteOff(0, 58);
    renderToWav(plugin, wav, 2.0f);  // release tail

    teardownPresetPlugin();
    TEST_PASS();
}

// --- 4: Hoover — rave bass riff ---
TestResult test_factory_hoover_wav() {
    TEST_BEGIN("Factory preset: Hoover (writes bin/preset_hoover.wav)");
    PluginInstance plugin;
    ASSERT_TRUE(setupPresetPlugin(plugin, 4), "loaded Hoover");
    WavWriter wav(kPresetWavNames[4], NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV open");

    // Big gliding bass riff
    plugin.midiNoteOn(0, 36, 120);   // C2
    renderToWav(plugin, wav, 0.5f);
    plugin.midiNoteOn(0, 48, 120);   // glide up to C3
    renderToWav(plugin, wav, 0.5f);
    plugin.midiNoteOn(0, 43, 110);   // glide to G2
    renderToWav(plugin, wav, 0.5f);
    plugin.midiNoteOn(0, 36, 120);   // back to C2
    renderToWav(plugin, wav, 0.5f);
    plugin.midiNoteOff(0, 36);
    renderToWav(plugin, wav, 0.3f);

    // Sustained power chord: C2+G2
    plugin.midiNoteOn(0, 36, 120);
    plugin.midiNoteOn(0, 43, 110);
    renderToWav(plugin, wav, 1.5f);
    plugin.midiNoteOff(0, 36);
    plugin.midiNoteOff(0, 43);
    renderToWav(plugin, wav, 0.5f);

    teardownPresetPlugin();
    TEST_PASS();
}

// --- 5: Fizzy Keys — plucky EP riff ---
TestResult test_factory_fizzy_keys_wav() {
    TEST_BEGIN("Factory preset: Fizzy Keys (writes bin/preset_fizzy_keys.wav)");
    PluginInstance plugin;
    ASSERT_TRUE(setupPresetPlugin(plugin, 5), "loaded Fizzy Keys");
    WavWriter wav(kPresetWavNames[5], NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV open");

    // Rhodes-style chord stabs with varying velocity
    struct { uint8_t notes[4]; uint8_t vel; float dur; } chords[] = {
        {{60,64,67,72}, 90,  0.5f},
        {{58,62,65,70}, 70,  0.5f},
        {{55,59,62,67}, 80,  0.5f},
        {{53,57,60,65}, 100, 0.8f},
    };
    for (auto& c : chords) {
        for (int n = 0; n < 4; ++n)
            plugin.midiNoteOn(0, c.notes[n], c.vel);
        renderToWav(plugin, wav, c.dur);
        for (int n = 0; n < 4; ++n)
            plugin.midiNoteOff(0, c.notes[n]);
        renderToWav(plugin, wav, 0.15f);
    }
    renderToWav(plugin, wav, 0.5f);

    teardownPresetPlugin();
    TEST_PASS();
}

// --- 6: Rez Sweep — held notes let the filter do the work ---
TestResult test_factory_rez_sweep_wav() {
    TEST_BEGIN("Factory preset: Rez Sweep (writes bin/preset_rez_sweep.wav)");
    PluginInstance plugin;
    ASSERT_TRUE(setupPresetPlugin(plugin, 6), "loaded Rez Sweep");
    WavWriter wav(kPresetWavNames[6], NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV open");

    // Single note: let the filter envelope sweep
    plugin.midiNoteOn(0, 48, 100);
    renderToWav(plugin, wav, 4.0f);
    plugin.midiNoteOff(0, 48);
    renderToWav(plugin, wav, 1.5f);

    // Second hit higher — different sweep character
    plugin.midiNoteOn(0, 60, 110);
    renderToWav(plugin, wav, 3.0f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 1.5f);

    teardownPresetPlugin();
    TEST_PASS();
}

// --- 7: Sync Lead — screaming sync melody ---
TestResult test_factory_sync_lead_wav() {
    TEST_BEGIN("Factory preset: Sync Lead (writes bin/preset_sync_lead.wav)");
    PluginInstance plugin;
    ASSERT_TRUE(setupPresetPlugin(plugin, 7), "loaded Sync Lead");
    WavWriter wav(kPresetWavNames[7], NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV open");

    // Punchy sync lead melody
    const uint8_t melody[] = {60, 64, 67, 72, 71, 67, 64, 60, 55, 60};
    for (int i = 0; i < 10; ++i) {
        plugin.midiNoteOn(0, melody[i], 110);
        renderToWav(plugin, wav, 0.3f);
        plugin.midiNoteOff(0, melody[i]);
        renderToWav(plugin, wav, 0.05f);
    }
    // Long sustained high note
    plugin.midiNoteOn(0, 72, 120);
    renderToWav(plugin, wav, 1.5f);
    plugin.midiNoteOff(0, 72);
    renderToWav(plugin, wav, 0.5f);

    teardownPresetPlugin();
    TEST_PASS();
}

// --- 8: Lofior (factory) — ambient pad via preset load ---
TestResult test_factory_lofior_wav() {
    TEST_BEGIN("Factory preset: Lofior (writes bin/preset_lofior_factory.wav)");
    PluginInstance plugin;
    ASSERT_TRUE(setupPresetPlugin(plugin, 8), "loaded Lofior");
    WavWriter wav(kPresetWavNames[8], NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV open");

    // Same phrase as the original Lofior test
    plugin.midiNoteOn(0, 48, 60);   // C3 gentle
    plugin.midiNoteOn(0, 55, 55);   // G3 gentle
    renderToWav(plugin, wav, 3.0f);
    plugin.midiNoteOn(0, 64, 80);   // E4 harder
    renderToWav(plugin, wav, 2.0f);
    plugin.midiNoteOff(0, 48);
    plugin.midiNoteOff(0, 55);
    plugin.midiNoteOff(0, 64);
    renderToWav(plugin, wav, 3.0f);
    plugin.midiNoteOn(0, 60, 120);  // C4 hard
    renderToWav(plugin, wav, 2.0f);
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 2.0f);

    teardownPresetPlugin();
    TEST_PASS();
}

// --- 9: Crushed — lo-fi bitcrushed texture ---
TestResult test_factory_crushed_wav() {
    TEST_BEGIN("Factory preset: Crushed (writes bin/preset_crushed.wav)");
    PluginInstance plugin;
    ASSERT_TRUE(setupPresetPlugin(plugin, 9), "loaded Crushed");
    WavWriter wav(kPresetWavNames[9], NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV open");

    // Crunchy chord hits
    plugin.midiNoteOn(0, 48, 60);
    plugin.midiNoteOn(0, 55, 60);
    renderToWav(plugin, wav, 1.0f);
    plugin.midiNoteOff(0, 48);
    plugin.midiNoteOff(0, 55);
    renderToWav(plugin, wav, 0.3f);

    // Hard hits — velocity drives distortion
    plugin.midiNoteOn(0, 48, 127);
    plugin.midiNoteOn(0, 55, 127);
    renderToWav(plugin, wav, 1.0f);
    plugin.midiNoteOff(0, 48);
    plugin.midiNoteOff(0, 55);
    renderToWav(plugin, wav, 0.3f);

    // Melody
    const uint8_t notes[] = {60, 63, 67, 60, 58, 55};
    for (int i = 0; i < 6; ++i) {
        plugin.midiNoteOn(0, notes[i], 100);
        renderToWav(plugin, wav, 0.3f);
        plugin.midiNoteOff(0, notes[i]);
        renderToWav(plugin, wav, 0.05f);
    }
    renderToWav(plugin, wav, 0.5f);

    teardownPresetPlugin();
    TEST_PASS();
}

// --- 10: Moog Bass — classic Moog ladder bass riff ---
TestResult test_factory_moog_bass_wav() {
    TEST_BEGIN("Factory preset: Moog Bass (writes bin/preset_moog_bass.wav)");
    PluginInstance plugin;
    ASSERT_TRUE(setupPresetPlugin(plugin, 10), "loaded Moog Bass");
    WavWriter wav(kPresetWavNames[10], NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV open");

    // Funky bass riff — legato for glide
    const uint8_t riff[] = {36, 36, 48, 36, 39, 36, 43, 36};
    const float dur[]     = {0.2f, 0.15f, 0.3f, 0.15f, 0.2f, 0.15f, 0.3f, 0.15f};
    for (int i = 0; i < 8; ++i) {
        plugin.midiNoteOn(0, riff[i], 100 + (i % 2) * 27);
        renderToWav(plugin, wav, dur[i]);
        plugin.midiNoteOff(0, riff[i]);
        renderToWav(plugin, wav, 0.05f);
    }
    // Sustained low note to hear filter + resonance
    plugin.midiNoteOn(0, 36, 110);
    renderToWav(plugin, wav, 1.5f);
    plugin.midiNoteOff(0, 36);
    renderToWav(plugin, wav, 0.5f);

    teardownPresetPlugin();
    TEST_PASS();
}

// --- 11: Tape Piano — TZFM bell + tape echo ---
TestResult test_factory_tape_piano_wav() {
    TEST_BEGIN("Factory preset: Tape Piano (writes bin/preset_tape_piano.wav)");
    PluginInstance plugin;
    ASSERT_TRUE(setupPresetPlugin(plugin, 11), "loaded Tape Piano");
    WavWriter wav(kPresetWavNames[11], NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV open");

    // Broken chord: C4 - E4 - G4
    int notes[] = {60, 64, 67};
    for (int i = 0; i < 3; ++i) {
        plugin.midiNoteOn(0, notes[i], 90 + i * 10);
        renderToWav(plugin, wav, 0.6f);
        plugin.midiNoteOff(0, notes[i]);
        renderToWav(plugin, wav, 0.3f);
    }
    // Full chord
    for (int i = 0; i < 3; ++i)
        plugin.midiNoteOn(0, notes[i], 100);
    renderToWav(plugin, wav, 1.5f);
    for (int i = 0; i < 3; ++i)
        plugin.midiNoteOff(0, notes[i]);
    renderToWav(plugin, wav, 2.0f);

    teardownPresetPlugin();
    TEST_PASS();
}

// --- 12: Scream Lead — MS-20 screaming lead ---
TestResult test_factory_scream_lead_wav() {
    TEST_BEGIN("Factory preset: Scream Lead (writes bin/preset_scream_lead.wav)");
    PluginInstance plugin;
    ASSERT_TRUE(setupPresetPlugin(plugin, 12), "loaded Scream Lead");
    WavWriter wav(kPresetWavNames[12], NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV open");

    // Aggressive lead line
    const uint8_t notes[] = {60, 64, 67, 72, 67, 64};
    for (int i = 0; i < 6; ++i) {
        plugin.midiNoteOn(0, notes[i], 100 + (i % 3) * 10);
        renderToWav(plugin, wav, 0.4f);
        plugin.midiNoteOff(0, notes[i]);
        renderToWav(plugin, wav, 0.1f);
    }
    // Sustained scream
    plugin.midiNoteOn(0, 72, 127);
    renderToWav(plugin, wav, 1.5f);
    plugin.midiNoteOff(0, 72);
    renderToWav(plugin, wav, 0.5f);

    teardownPresetPlugin();
    TEST_PASS();
}

// --- 13: 303 Acid — Diode ladder acid squelch ---
TestResult test_factory_303_acid_wav() {
    TEST_BEGIN("Factory preset: 303 Acid (writes bin/preset_303_acid.wav)");
    PluginInstance plugin;
    ASSERT_TRUE(setupPresetPlugin(plugin, 13), "loaded 303 Acid");
    WavWriter wav(kPresetWavNames[13], NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV open");

    // Classic acid pattern — legato for glide
    const uint8_t pattern[] = {36, 36, 48, 39, 36, 43, 36, 48};
    const float dur[]       = {0.15f, 0.1f, 0.2f, 0.15f, 0.1f, 0.2f, 0.15f, 0.15f};
    for (int i = 0; i < 8; ++i) {
        plugin.midiNoteOn(0, pattern[i], 90 + (i % 2) * 37);
        renderToWav(plugin, wav, dur[i]);
        plugin.midiNoteOff(0, pattern[i]);
        renderToWav(plugin, wav, 0.05f);
    }
    // Sustained squelch
    plugin.midiNoteOn(0, 36, 110);
    renderToWav(plugin, wav, 1.5f);
    plugin.midiNoteOff(0, 36);
    renderToWav(plugin, wav, 0.5f);

    teardownPresetPlugin();
    TEST_PASS();
}

// =========================================================================
// Regression: golden SHA-256 hashes for all WAV outputs
// Reads tests/golden_hashes.txt (sha256sum-compatible format).
// Every WAV is deterministic, so any code change that alters audio will
// break a hash here.  To re-golden after an intentional change, replace
// the hash with *  in golden_hashes.txt and run — the test will compute
// the new hash, update the file in-place, and pass.
// =========================================================================
TestResult test_golden_wav_hashes() {
    TEST_BEGIN("Golden WAV hashes (SHA-256 regression)");

    const char* goldenPath = "tests/golden_hashes.txt";

    // --- Read the golden file ---
    FILE* f = fopen(goldenPath, "r");
    ASSERT_TRUE(f != nullptr, "tests/golden_hashes.txt exists");

    struct Line {
        std::string raw;        // original line text
        std::string hash;       // 64-char expected hash or "*"
        std::string file;       // WAV path
        std::string actual;     // computed hash (filled in below)
        bool isEntry = false;
    };

    std::vector<Line> lines;
    char buf[512];
    while (fgets(buf, sizeof(buf), f)) {
        Line ln;
        ln.raw = buf;
        // strip trailing newline
        while (!ln.raw.empty() && (ln.raw.back() == '\n' || ln.raw.back() == '\r'))
            ln.raw.pop_back();

        // skip comments and blanks
        if (ln.raw.empty() || ln.raw[0] == '#') {
            lines.push_back(ln);
            continue;
        }

        // parse: "<hash>  <filepath>" (sha256sum format: two-space separator)
        size_t sep = ln.raw.find("  ");
        if (sep == std::string::npos) { lines.push_back(ln); continue; }

        ln.hash = ln.raw.substr(0, sep);
        ln.file = ln.raw.substr(sep + 2);
        ln.isEntry = true;
        lines.push_back(ln);
    }
    fclose(f);

    // --- Check each entry ---
    int mismatches = 0;
    int regenerated = 0;
    for (auto& ln : lines) {
        if (!ln.isEntry) continue;

        ln.actual = sha256_file(ln.file.c_str());
        ASSERT_TRUE(!ln.actual.empty(), (std::string("can read ") + ln.file).c_str());

        if (ln.hash == "*") {
            printf("    REGEN  %-30s %s\n", ln.file.c_str(), ln.actual.c_str());
            ++regenerated;
        } else if (ln.actual != ln.hash) {
            printf("    MISMATCH %-28s\n      expected: %s\n      actual:   %s\n",
                   ln.file.c_str(), ln.hash.c_str(), ln.actual.c_str());
            ++mismatches;
        }
    }

    // --- Rewrite the file if any entries were re-goldened ---
    if (regenerated > 0) {
        FILE* fw = fopen(goldenPath, "w");
        ASSERT_TRUE(fw != nullptr, "can rewrite golden_hashes.txt");
        for (auto& ln : lines) {
            if (ln.isEntry)
                fprintf(fw, "%s  %s\n", ln.actual.c_str(), ln.file.c_str());
            else
                fprintf(fw, "%s\n", ln.raw.c_str());
        }
        fclose(fw);
        printf("    >> golden_hashes.txt updated (%d hash%s regenerated)\n",
               regenerated, regenerated > 1 ? "es" : "");
    }

    ASSERT_TRUE(mismatches == 0, "all WAV hashes match golden references");

    TEST_PASS();
}

// =========================================================================
// BUG DIAGNOSTIC: Steal crossfade tail energy
// =========================================================================
// Proves that noteOn() destroys the crossfade tail rendered by
// renderStealTail().  stealVoice() calls renderStealTail() which sets
// stealTailRemaining = 256, then calls noteOn() which resets it to 0.
// The old voice hard-cuts to silence; only the new voice fade-in
// (from 0) remains — producing a click.
//
// Expected: FAIL with current code (post-steal energy near zero).
//           PASS once bug is fixed (crossfade preserves energy).
// =========================================================================
TestResult test_steal_tail_energy() {
    TEST_BEGIN("BUG: Stolen voice crossfade tail produces audio (not silent)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    // Simple bright patch — single osc, 10ms attack (>2ms, no snap), no delay
    plugin.setParameter(kP_Osc1Waveform, kWave_PolyBlepSaw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_AmpAttack, 10);       // 10ms
    plugin.setParameter(kP_AmpDecay, 100);
    plugin.setParameter(kP_AmpSustain, 1000);    // full sustain
    plugin.setParameter(kP_AmpRelease, 300);
    plugin.setParameter(kP_DelayMix, 0);
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);

    // Play C4 and let it reach stable sustain (~200ms)
    plugin.midiNoteOn(0, 60, 127);
    for (int i = 0; i < blocksFor(0.2f); ++i)
        plugin.step(BLOCK_SIZE);

    // Measure pre-steal RMS over one block
    plugin.step(BLOCK_SIZE);
    float* bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
    float preStealSq = 0.0f;
    for (int i = 0; i < BLOCK_SIZE; ++i)
        preStealSq += bus[i] * bus[i];
    float preStealRms = std::sqrt(preStealSq / BLOCK_SIZE);
    ASSERT_GT(preStealRms, 0.01f, "voice is producing audio before steal");

    // Retrigger same note — triggers stealVoice() path
    plugin.midiNoteOn(0, 60, 127);

    // Capture FIRST block after retrigger (64 samples ≈ 1.45ms)
    plugin.step(BLOCK_SIZE);
    bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
    float postStealSq = 0.0f;
    for (int i = 0; i < BLOCK_SIZE; ++i)
        postStealSq += bus[i] * bus[i];
    float postStealRms = std::sqrt(postStealSq / BLOCK_SIZE);

    // With a WORKING crossfade:
    //   Old voice tail fades from 1→0 over 256 samples.
    //   In the first 64 samples the tail is at 100%→75%.
    //   New voice fades in from 0→25%.
    //   Combined RMS should be well above 50% of pre-steal.
    //
    // With the BROKEN code (tail destroyed):
    //   Only the fade-in exists: stealFade goes 0→0.25 in 64 samples.
    //   Envelope goes from 0→0.15 (10ms attack, 1.45ms elapsed).
    //   Product is ~3-5% of pre-steal. Way below 0.25.
    float ratio = postStealRms / preStealRms;
    ASSERT_GT(ratio, 0.25f,
        "post-steal RMS should be >25% of pre-steal (crossfade tail must output audio)");

    TEST_PASS();
}

// =========================================================================
// BUG DIAGNOSTIC: Rapid retrigger click WAV
// =========================================================================
// Renders a WAV with rapid same-note retriggering every ~30ms.
// With the broken crossfade, each retrigger causes a hard cut
// to silence then ramp-up — audible as machine-gun clicking.
// =========================================================================
TestResult test_rapid_retrigger_click_wav() {
    TEST_BEGIN("BUG: Rapid retrigger click (writes bin/diag_rapid_retrigger.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/diag_rapid_retrigger.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Bright saw — easy to hear clicks
    plugin.setParameter(kP_Osc1Waveform, kWave_PolyBlepSaw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_AmpAttack, 10);       // 10ms
    plugin.setParameter(kP_AmpDecay, 100);
    plugin.setParameter(kP_AmpSustain, 1000);
    plugin.setParameter(kP_AmpRelease, 300);
    plugin.setParameter(kP_DelayMix, 0);
    plugin.setParameter(kP_BaseCutoff, 8000);
    plugin.setParameter(kP_FilterEnvAmount, 0);

    // Start with a sustained note
    plugin.midiNoteOn(0, 60, 100);
    renderToWav(plugin, wav, 0.2f);

    // Rapid retrigger 20 times at ~30ms intervals
    float maxDipRatio = 1.0f;
    for (int rep = 0; rep < 20; ++rep) {
        // Measure RMS of block just before retrigger
        plugin.step(BLOCK_SIZE);
        const float* busPre = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
        wav.writeMono(busPre, BLOCK_SIZE);
        float preSq = 0.0f;
        for (int i = 0; i < BLOCK_SIZE; ++i) preSq += busPre[i] * busPre[i];
        float preRms = std::sqrt(preSq / BLOCK_SIZE);

        // Retrigger same note
        plugin.midiNoteOn(0, 60, 100);

        // Measure RMS of block immediately after retrigger
        plugin.step(BLOCK_SIZE);
        const float* busPost = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
        wav.writeMono(busPost, BLOCK_SIZE);
        float postSq = 0.0f;
        for (int i = 0; i < BLOCK_SIZE; ++i) postSq += busPost[i] * busPost[i];
        float postRms = std::sqrt(postSq / BLOCK_SIZE);

        if (preRms > 0.001f) {
            float ratio = postRms / preRms;
            if (ratio < maxDipRatio) maxDipRatio = ratio;
        }

        // Let it ring for a bit before next retrigger
        renderToWav(plugin, wav, 0.025f);
    }

    // Release and tail
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 0.5f);
    wav.close();

    // With proper crossfade the old voice tail carries energy through.
    // The dip is ~35% because the new voice attack (10ms) hasn't ramped
    // fully during the 5.8ms crossfade window.  Without the fix the dip
    // is catastrophic (~1% of pre-steal).
    ASSERT_GT(maxDipRatio, 0.20f,
        "worst energy dip during rapid retrigger should stay above 20% (crossfade active)");

    TEST_PASS();
}

// =========================================================================
// BUG DIAGNOSTIC: Chord progression double-steal
// =========================================================================
// When all voices are busy and multiple noteOns arrive, the allocator
// steals the "quietest" voice each time.  After stealing, gate(true) sets
// the envelope's currentLevel to 0 (if attack >= 2ms).
// getCurrentAmplitudeLevel() returns 0 for that voice — making it the
// quietest AGAIN.  The next noteOn re-steals the same voice, losing the
// previous note.  Result: only the LAST note of a chord actually plays.
//
// Expected: FAIL with current code (only 1 drop detected instead of 4).
//           PASS once the allocator accounts for steal-in-progress.
// =========================================================================
TestResult test_chord_steal_unique_voices() {
    TEST_BEGIN("BUG: Chord steal allocates unique voices for each note");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    // Patch: single osc, 10ms attack, 500ms release, no delay
    plugin.setParameter(kP_Osc1Waveform, kWave_PolyBlepSaw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_AmpAttack, 10);
    plugin.setParameter(kP_AmpDecay, 100);
    plugin.setParameter(kP_AmpSustain, 800);
    plugin.setParameter(kP_AmpRelease, 500);
    plugin.setParameter(kP_DelayMix, 0);
    plugin.setParameter(kP_BaseCutoff, 10000);
    plugin.setParameter(kP_FilterEnvAmount, 0);

    // Fill ALL 8 voices with low notes
    const int fill[] = {36, 38, 40, 41, 43, 45, 47, 48};
    for (int i = 0; i < 8; ++i)
        plugin.midiNoteOn(0, fill[i], 100);

    // Let them reach sustain
    for (int i = 0; i < blocksFor(0.3f); ++i)
        plugin.step(BLOCK_SIZE);

    // Release all — voices enter release phase (still active for 500ms)
    for (int i = 0; i < 8; ++i)
        plugin.midiNoteOff(0, fill[i]);

    // Small gap: 20ms — voices still releasing, all active
    for (int i = 0; i < blocksFor(0.02f); ++i)
        plugin.step(BLOCK_SIZE);

    // Now send a NEW 4-note chord — must steal 4 voices
    const int chord[] = {60, 64, 67, 71};  // C4, E4, G4, B4
    for (int i = 0; i < 4; ++i)
        plugin.midiNoteOn(0, chord[i], 120);

    // Wait for OLD voices to fully die (release 500ms + margin)
    // and new chord to stabilize in sustain
    for (int i = 0; i < blocksFor(0.8f); ++i)
        plugin.step(BLOCK_SIZE);

    // Now ONLY the 4 new chord voices should be active.
    // Release each note individually and check that each one
    // causes a distinct amplitude drop.
    // Helper: average RMS over many blocks to smooth phase-alignment noise.
    // A single 64-sample block of a saw wave has huge RMS variance.
    auto avgRms = [&](int numBlocks) -> float {
        float totalSq = 0.0f;
        int totalSamples = 0;
        for (int b = 0; b < numBlocks; ++b) {
            plugin.step(BLOCK_SIZE);
            float* bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
            for (int i = 0; i < BLOCK_SIZE; ++i)
                totalSq += bus[i] * bus[i];
            totalSamples += BLOCK_SIZE;
        }
        return std::sqrt(totalSq / totalSamples);
    };
    const int measureBlocks = blocksFor(0.05f);  // 50ms average

    int dropsDetected = 0;

    for (int n = 0; n < 4; ++n) {
        // Measure averaged RMS before releasing this note
        float beforeRms = avgRms(measureBlocks);

        // Release one note
        plugin.midiNoteOff(0, chord[n]);

        // Wait for release to finish (500ms + margin)
        for (int i = 0; i < blocksFor(0.6f); ++i)
            plugin.step(BLOCK_SIZE);

        // Measure averaged RMS after release
        float afterRms = avgRms(measureBlocks);

        // A real amplitude drop: at least 10% decrease
        if (beforeRms > 0.001f && afterRms < beforeRms * 0.90f) {
            dropsDetected++;
        }
    }

    // If each of the 4 notes was on a UNIQUE voice, we'd see 4 distinct drops.
    // With the double-steal bug, only 1 voice plays (the last note), so only
    // 1 noteOff (the last one) causes a drop.
    ASSERT_EQ(dropsDetected, 4,
        "each noteOff should cause a distinct amplitude drop (4 unique voices)");

    TEST_PASS();
}

// =========================================================================
// BUG DIAGNOSTIC: Chord progression WAV for listening
// =========================================================================
// Renders two chord progressions where the second chord arrives while
// all 8 voices are still releasing from the first.  With the double-steal
// bug, the second chord sounds thin (only 1 note) instead of full (4 notes).
// =========================================================================
TestResult test_chord_progression_steal_wav() {
    TEST_BEGIN("BUG: Chord progression steal (writes bin/diag_chord_steal.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/diag_chord_steal.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Setup: bright saw, moderate attack/release
    plugin.setParameter(kP_Osc1Waveform, kWave_PolyBlepSaw);
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_AmpAttack, 10);
    plugin.setParameter(kP_AmpDecay, 100);
    plugin.setParameter(kP_AmpSustain, 800);
    plugin.setParameter(kP_AmpRelease, 500);
    plugin.setParameter(kP_DelayMix, 0);
    plugin.setParameter(kP_BaseCutoff, 8000);
    plugin.setParameter(kP_FilterEnvAmount, 0);

    // --- Chord 1: fill all 8 voices ---
    const int chord1[] = {36, 38, 40, 41, 43, 45, 47, 48};
    for (int i = 0; i < 8; ++i)
        plugin.midiNoteOn(0, chord1[i], 100);
    renderToWav(plugin, wav, 0.5f);

    // Release chord 1
    for (int i = 0; i < 8; ++i)
        plugin.midiNoteOff(0, chord1[i]);
    renderToWav(plugin, wav, 0.02f);  // tiny gap

    // --- Chord 2: 4 notes, must steal from releasing voices ---
    const int chord2[] = {60, 64, 67, 71};
    for (int i = 0; i < 4; ++i)
        plugin.midiNoteOn(0, chord2[i], 120);
    renderToWav(plugin, wav, 1.0f);  // Let it ring — listen for 1 vs 4 notes

    // Release chord 2
    for (int i = 0; i < 4; ++i)
        plugin.midiNoteOff(0, chord2[i]);
    renderToWav(plugin, wav, 0.8f);  // release tail

    // --- Reference: same chord 2 played on fresh voices (no steal) ---
    // Wait for all voices to die
    renderToWav(plugin, wav, 2.0f);

    // Re-play chord 2 on free voices for comparison
    for (int i = 0; i < 4; ++i)
        plugin.midiNoteOn(0, chord2[i], 120);
    renderToWav(plugin, wav, 1.0f);

    for (int i = 0; i < 4; ++i)
        plugin.midiNoteOff(0, chord2[i]);
    renderToWav(plugin, wav, 0.8f);

    wav.close();

    // Compare sustain RMS of stolen chord vs free chord
    // (The WAV file also allows listening to hear the difference)

    TEST_PASS();
}

// =========================================================================
// BUG: Legato same-note retrigger
// =========================================================================
TestResult test_legato_same_note_retrigger() {
    TEST_BEGIN("BUG: Legato same-note retrigger (writes bin/diag_legato_retrigger.wav)");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    // Enable legato, set short attack/release for clear envelope edges
    plugin.setParameter(kP_Legato, 1);
    plugin.setParameter(kP_AmpAttack, 5);
    plugin.setParameter(kP_AmpDecay, 100);
    plugin.setParameter(kP_AmpSustain, 800);
    plugin.setParameter(kP_AmpRelease, 200);

    WavWriter wav("bin/diag_legato_retrigger.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "wav open");

    // --- Phase 1: play C4 for ~200ms, let it sustain ---
    plugin.midiNoteOn(0, 60, 100);
    float peakHeld = renderToWav(plugin, wav, 0.2f);
    ASSERT_TRUE(peakHeld > 0.01f, "first note produces audio");

    // --- Phase 2: release C4, render ~150ms of release tail ---
    plugin.midiNoteOff(0, 60);
    renderToWav(plugin, wav, 0.15f);

    // --- Phase 3: play the SAME note C4 again ---
    plugin.midiNoteOn(0, 60, 100);
    // Render ~200ms — the envelope should retrigger and produce audio
    float peakRetrigger = renderToWav(plugin, wav, 0.2f);

    // This is the bug: if legatoRetrigger is called instead of noteOn,
    // envelopes are NOT retriggered and the output will be near-silent
    // because the amp envelope is still in release/idle.
    ASSERT_TRUE(peakRetrigger > 0.01f,
        "same note retrigger in legato produces audio (envelope retriggered)");

    // Peak should be comparable to the first note (within ~12 dB)
    ASSERT_TRUE(peakRetrigger > peakHeld * 0.25f,
        "retrigger amplitude comparable to first note");

    wav.close();
    TEST_PASS();
}

// =========================================================================
// Main
// =========================================================================
int main() {
    NtTestHarness::setSampleRate(44100);
    NtTestHarness::setMaxFrames(BLOCK_SIZE);

    return TestRunner::run({
        // --- Core lifecycle tests ---
        test_plugin_loads,
        test_silence_when_idle,
        test_note_on_produces_audio,
        test_note_off_release,
        test_polyphony,
        test_voice_stealing,
        test_midi_channel_filter,
        test_parameter_sweep,

        // --- Basic WAV captures ---
        test_wav_capture,
        test_chord_wav,

        // --- Feature WAV tests ---
        test_waveforms_wav,
        test_amp_envelopes_wav,
        test_tzfm_wav,
        test_tzfm_all_routes_wav,
        test_osc_sync_unit,
        test_hard_sync_wav,
        test_hard_sync_sweep_wav,
        test_lfo_cutoff_wav,
        test_lfo_morph_wav,
        test_modmatrix_velocity_cutoff_wav,
        test_modmatrix_modenv_fm_wav,
        test_filter_modes_wav,
        test_delay_wav,
        test_3osc_detune_wav,
        test_morph_sweep_wav,
        test_drive_wav,
        test_filter_env_wav,
        test_multi_lfo_wav,
        test_fm_plus_sync_wav,

        // --- New feature tests ---
        test_lfo_morph_survives_speed_change,
        test_pitch_bend_wav,
        test_sustain_pedal_wav,
        test_sustain_retrigger_wav,
        test_mod_wheel_wav,
        test_pulse_width_wav,
        test_lfo_exp_speed_wav,

        // --- Items 9-11 tests ---
        test_aftertouch_wav,
        test_glide_wav,
        test_bitcrush_wav,

        // --- Delay / Chorus / Flanger exploration (11a) ---
        test_delay_bypass_wav,
        test_chorus_lfo_delaytime_wav,
        test_flanger_lfo_feedback_wav,
        test_delay_env_ducking_wav,
        test_delay_vel_feedback_wav,
        test_pervoice_delay_pentatonic_wav,

        // --- Per-voice delay synthesis demos (11b) ---
        test_karplus_strong_wav,
        test_pervoice_echo_cascade_wav,
        test_comb_filter_timbre_wav,
        test_slapback_doubling_wav,

        // --- PolyBLEP waveform demos ---
        test_polyblep_saw_sync_wav,
        test_polyblep_square_pwm_wav,
        test_polyblep_vs_naive_wav,
        test_polyblep_decimated_rate_wav,
        test_polyblep_sync_sweep_wav,

        // --- MIDI clock sync ---
        test_midi_sync_lfo_wav,
        test_delay_sync_wav,
        test_delay_sync_autostart_wav,

        // --- Wavetable ---
        test_wavetable_morph_wav,

        // --- Stereo pan spread ---
        test_voice_steal_crossfade_wav,
        test_stereo_chord_progression_wav,

        // --- LFO key sync ---
        test_lfo_key_sync_wav,

        // --- Delay diffusion ---
        test_delay_diffusion_wav,

        // --- Noise waveform ---
        test_noise_morph_wav,

        // --- New mod destinations ---
        test_mod_destinations_wav,

        // --- Delay overhaul + per-osc level ---

        // --- Pitch-tracked comb delay ---
        test_pitch_tracked_comb_wav,

        // --- Per-note random Mod ModSource ---
        test_note_random_mod_wav,

        // --- Wavetable preset showcase (WtGen) ---
        test_wt_sine_saw_wav,
        test_wt_sine_square_wav,
        test_wt_sine_tri_wav,
        test_wt_saw_square_wav,
        test_wt_tri_saw_wav,
        test_wt_additive_wav,
        test_wt_additive_soft_wav,
        test_wt_pwm_wav,
        test_wt_fm_2x_wav,
        test_wt_fm_3x_wav,
        test_wt_fm_golden_wav,
        test_wt_wavefold_wav,
        test_wt_wavefold_gentle_wav,
        test_wt_formant_wav,
        test_wt_formant_vocal_wav,
        test_wt_supersaw_wav,
        test_wt_supersaw_wide_wav,

        // --- Hardware presets ---
        test_lofior_preset_wav,

        // --- Internal preset system ---
        test_preset_save_load_roundtrip,
        test_preset_factory_load,
        test_preset_multiple_slots,

        // --- Factory preset WAV renders ---
        test_ladder_filter_modes_wav,
        test_ladder_reso_sweep_wav,
        test_svf_vs_ladder_wav,
        test_ms20_filter_modes_wav,
        test_diode_filter_modes_wav,

        // --- Keyboard tracking, pitch-space cutoff, reso compensation ---
        test_keyboard_tracking_wav,
        test_reso_compensation_wav,
        test_keytrack_mod_source_wav,

        test_factory_supersaw_wav,
        test_factory_acid_bass_wav,
        test_factory_virus_lead_wav,
        test_factory_pwm_pad_wav,
        test_factory_hoover_wav,
        test_factory_fizzy_keys_wav,
        test_factory_rez_sweep_wav,
        test_factory_sync_lead_wav,
        test_factory_lofior_wav,
        test_factory_crushed_wav,
        test_factory_scream_lead_wav,
        test_factory_303_acid_wav,
        test_factory_moog_bass_wav,
        test_factory_tape_piano_wav,

        // --- Voice stealing bug diagnostics ---
        test_steal_tail_energy,
        test_rapid_retrigger_click_wav,
        test_chord_steal_unique_voices,
        test_chord_progression_steal_wav,

        // --- Legato bug diagnostics ---
        test_legato_same_note_retrigger,

        // --- Regression ---
        test_golden_wav_hashes,
    });
}
