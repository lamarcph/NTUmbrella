// =============================================================================
// NoiseBouquet/tests/test_effect_units.cpp
// =============================================================================
// Focused host-side regression tests for the M3 effect layer.
//
// These exercise the vendored Teensy effects directly rather than routing them
// through a plugin, which keeps M3 isolated from the Bank/Program framework.
// Each test renders a short deterministic WAV and asserts the effect is both
// audible and meaningfully different from its dry input.
// =============================================================================

#include "../../test_harness/test_framework.h"
#include "../../test_harness/plugin_harness.h"
#include "../../test_harness/wav_writer.h"

#include "../include/nt_rack_shim.hpp"
#include "../teensy/dspinst.h"
#include "../teensy/synth_dc.hpp"
#include "../teensy/synth_waveform.hpp"
#include "../teensy/effect_bitcrusher.h"
#include "../teensy/effect_combine.hpp"
#include "../teensy/effect_multiply.h"
#include "../teensy/effect_wavefolder.hpp"

#include <cmath>
#include <cstring>

namespace {

static constexpr int BLOCK_SIZE = AUDIO_BLOCK_SAMPLES;

static int blocksFor(float seconds) {
    return static_cast<int>(NtTestHarness::getSampleRate() * seconds / BLOCK_SIZE);
}

static void prepareStandaloneAudio() {
    nt_shim::setSampleRate(static_cast<float>(NtTestHarness::getSampleRate()));
    teensy::seed = 1;
}

static void blockToFloat(const audio_block_t& block, float* out) {
    for (int i = 0; i < BLOCK_SIZE; ++i) {
        out[i] = int16_to_float_1v(block.data[i]);
    }
}

static float rmsDiff(const audio_block_t& a, const audio_block_t& b) {
    double sum = 0.0;
    for (int i = 0; i < BLOCK_SIZE; ++i) {
        double d = (double)a.data[i] - (double)b.data[i];
        sum += d * d;
    }
    return (float)std::sqrt(sum / BLOCK_SIZE) / 32768.0f;
}

} // namespace

TestResult test_effect_bitcrusher_wav() {
    TEST_BEGIN("Render M3 bitcrusher demo -> bin/effect_bitcrusher.wav");
    prepareStandaloneAudio();

    AudioSynthWaveform source;
    AudioEffectBitcrusher effect;
    source.begin(0.95f, 880.0f, WAVEFORM_SAWTOOTH);
    effect.bits(5);
    effect.sampleRate(3200.0f);

    WavWriter wav("bin/effect_bitcrusher.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    float peak = 0.0f;
    float maxDiff = 0.0f;
    float out[BLOCK_SIZE];
    audio_block_t dry = {}, wet = {};
    int n = blocksFor(1.0f);
    for (int i = 0; i < n; ++i) {
        source.update(&dry);
        effect.update(&dry, &wet);
        float d = rmsDiff(dry, wet);
        if (d > maxDiff) maxDiff = d;
        blockToFloat(wet, out);
        wav.writeMono(out, BLOCK_SIZE);
        float p = PluginInstance::peak(out, BLOCK_SIZE);
        if (p > peak) peak = p;
    }
    wav.close();

    ASSERT_GT(peak, 0.05f, "bitcrusher output is audible");
    ASSERT_GT(maxDiff, 0.005f, "bitcrusher meaningfully alters waveform");
    TEST_PASS();
}

TestResult test_effect_combine_wav() {
    TEST_BEGIN("Render M3 digital combine demo -> bin/effect_combine.wav");
    prepareStandaloneAudio();

    AudioSynthWaveform sourceA;
    AudioSynthWaveform sourceB;
    AudioEffectDigitalCombine effect;
    sourceA.begin(0.9f, 330.0f, WAVEFORM_SQUARE);
    sourceB.begin(0.9f, 495.0f, WAVEFORM_SAWTOOTH);
    effect.setCombineMode(AudioEffectDigitalCombine::XOR);

    WavWriter wav("bin/effect_combine.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    float peak = 0.0f;
    float maxDiff = 0.0f;
    float out[BLOCK_SIZE];
    audio_block_t inA = {}, inB = {}, wet = {};
    int n = blocksFor(1.0f);
    for (int i = 0; i < n; ++i) {
        sourceA.update(&inA);
        sourceB.update(&inB);
        effect.update(&inA, &inB, &wet);
        float d = rmsDiff(inA, wet);
        if (d > maxDiff) maxDiff = d;
        blockToFloat(wet, out);
        wav.writeMono(out, BLOCK_SIZE);
        float p = PluginInstance::peak(out, BLOCK_SIZE);
        if (p > peak) peak = p;
    }
    wav.close();

    ASSERT_GT(peak, 0.05f, "combine output is audible");
    ASSERT_GT(maxDiff, 0.01f, "combine meaningfully alters waveform");
    TEST_PASS();
}

TestResult test_effect_multiply_wav() {
    TEST_BEGIN("Render M3 multiply demo -> bin/effect_multiply.wav");
    prepareStandaloneAudio();

    AudioSynthWaveform carrier;
    AudioSynthWaveform modulator;
    AudioEffectMultiply effect;
    carrier.begin(0.95f, 220.0f, WAVEFORM_SINE);
    modulator.begin(0.95f, 47.0f, WAVEFORM_TRIANGLE);

    WavWriter wav("bin/effect_multiply.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    float peak = 0.0f;
    float maxDiff = 0.0f;
    float out[BLOCK_SIZE];
    audio_block_t inA = {}, inB = {}, wet = {};
    int n = blocksFor(1.0f);
    for (int i = 0; i < n; ++i) {
        carrier.update(&inA);
        modulator.update(&inB);
        effect.update(&inA, &inB, &wet);
        float d = rmsDiff(inA, wet);
        if (d > maxDiff) maxDiff = d;
        blockToFloat(wet, out);
        wav.writeMono(out, BLOCK_SIZE);
        float p = PluginInstance::peak(out, BLOCK_SIZE);
        if (p > peak) peak = p;
    }
    wav.close();

    ASSERT_GT(peak, 0.02f, "multiply output is audible");
    ASSERT_GT(maxDiff, 0.01f, "multiply meaningfully alters waveform");
    TEST_PASS();
}

TestResult test_effect_wavefolder_wav() {
    TEST_BEGIN("Render M3 wavefolder demo -> bin/effect_wavefolder.wav");
    prepareStandaloneAudio();

    AudioSynthWaveform source;
    AudioSynthWaveformDc fold;
    AudioEffectWaveFolder effect;
    source.begin(0.95f, 330.0f, WAVEFORM_SINE);
    fold.amplitude(0.7f);

    WavWriter wav("bin/effect_wavefolder.wav", NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    float peak = 0.0f;
    float maxDiff = 0.0f;
    float out[BLOCK_SIZE];
    audio_block_t dry = {}, ctl = {}, wet = {};
    int n = blocksFor(1.0f);
    for (int i = 0; i < n; ++i) {
        source.update(&dry);
        fold.update(&ctl);
        effect.update(&dry, &ctl, &wet);
        float d = rmsDiff(dry, wet);
        if (d > maxDiff) maxDiff = d;
        blockToFloat(wet, out);
        wav.writeMono(out, BLOCK_SIZE);
        float p = PluginInstance::peak(out, BLOCK_SIZE);
        if (p > peak) peak = p;
    }
    wav.close();

    ASSERT_GT(peak, 0.02f, "wavefolder output is audible");
    ASSERT_GT(maxDiff, 0.005f, "wavefolder meaningfully alters waveform");
    TEST_PASS();
}
