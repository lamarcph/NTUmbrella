// =============================================================================
// LofiOsc Integration Tests — uses shared NTUmbrella test harness
// =============================================================================
// Build:  make test
// Run:    make test-run
// =============================================================================

#include "test_framework.h"
#include "plugin_harness.h"
#include "wav_writer.h"
#include "sha256.h"

#include <cmath>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

// -------------------------------------------------------------------------
// Constants
// -------------------------------------------------------------------------
static constexpr int BLOCK_SIZE  = 128;
static constexpr int SAMPLE_RATE = 44100;
static constexpr int OUTPUT_BUS  = 1;  // 1-based bus index

// Parameter indices (mirror LofiOsciallators.cpp enum)
enum {
    kP_Output = 0,
    kP_OutputMode,
    kP_LinFMInput,
    kP_V8Input,
    kP_MorphInput,
    kP_HarmonicsInput,
    kP_Waveform,
    kP_OscSemi,
    kP_OscFine,
    kP_OscV8c,
    kP_OscMorph,
    kP_Detune,
    kP_DetuneType,
    kP_Gain,
    kP_FmDepth,
    kP_MorphModDepth,
    kP_Harmonics,
};

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------
static int blocksFor(float seconds) {
    return static_cast<int>(seconds * SAMPLE_RATE / BLOCK_SIZE);
}

static bool createPlugin(PluginInstance& plugin) {
    if (!plugin.load(0)) return false;
    if (!plugin.construct()) return false;
    plugin.setParameter(kP_Output, 1);   // Route output to bus 0
    return true;
}

/// Render plugin audio to WAV for `seconds`, returns peak over the duration.
static float renderToWav(PluginInstance& plugin, WavWriter& wav, float seconds) {
    float peakAll = 0.0f;
    int n = blocksFor(seconds);
    for (int i = 0; i < n; ++i) {
        plugin.step(BLOCK_SIZE);
        const float* bus = plugin.getBus(OUTPUT_BUS - 1, BLOCK_SIZE);
        wav.writeMono(bus, BLOCK_SIZE);
        float p = PluginInstance::peak(bus, BLOCK_SIZE);
        if (p > peakAll) peakAll = p;
    }
    return peakAll;
}

// -------------------------------------------------------------------------
// Tests
// -------------------------------------------------------------------------

TestResult test_plugin_loads() {
    TEST_BEGIN("Plugin loads and constructs");
    PluginInstance plugin;
    ASSERT_TRUE(plugin.load(0), "factory loads");
    ASSERT_TRUE(plugin.construct(), "construct succeeds");
    ASSERT_TRUE(plugin.name() != nullptr, "plugin has a name");
    TEST_PASS();
}

TestResult test_produces_audio() {
    TEST_BEGIN("Default config produces audio");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    // Plugin is always-on (no MIDI gate), so stepping should produce audio
    float peakTotal = 0.0f;
    for (int b = 0; b < blocksFor(0.1f); ++b) {
        float* bus = plugin.step(BLOCK_SIZE);
        float* out = bus + (OUTPUT_BUS - 1) * BLOCK_SIZE;
        peakTotal = std::max(peakTotal, PluginInstance::peak(out, BLOCK_SIZE));
    }
    ASSERT_GT(peakTotal, 0.001f, "audio output is non-zero");
    TEST_PASS();
}

TestResult test_silence_when_gain_zero() {
    TEST_BEGIN("Silence when gain is zero");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");
    plugin.setParameter(kP_Gain, 0);

    float peakTotal = 0.0f;
    for (int b = 0; b < blocksFor(0.1f); ++b) {
        float* bus = plugin.step(BLOCK_SIZE);
        float* out = bus + (OUTPUT_BUS - 1) * BLOCK_SIZE;
        peakTotal = std::max(peakTotal, PluginInstance::peak(out, BLOCK_SIZE));
    }
    ASSERT_LT(peakTotal, 0.0001f, "output should be silent at gain=0");
    TEST_PASS();
}

TestResult test_all_waveforms_produce_audio() {
    TEST_BEGIN("All waveforms produce audio");
    const char* waveNames[] = {"Sine", "Square", "Triangle", "Sawtooth", "Morph"};

    for (int w = 0; w <= 4; ++w) {
        PluginInstance plugin;
        ASSERT_TRUE(createPlugin(plugin), "plugin created");
        plugin.setParameter(kP_Waveform, w);

        float peakTotal = 0.0f;
        for (int b = 0; b < blocksFor(0.05f); ++b) {
            float* bus = plugin.step(BLOCK_SIZE);
            float* out = bus + (OUTPUT_BUS - 1) * BLOCK_SIZE;
            peakTotal = std::max(peakTotal, PluginInstance::peak(out, BLOCK_SIZE));
        }
        ASSERT_GT(peakTotal, 0.001f,
            std::string("waveform ") + waveNames[w] + " should produce audio");
    }
    TEST_PASS();
}

TestResult test_sweep_params_no_crash() {
    TEST_BEGIN("Sweeping all parameters doesn't crash");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    int numParams = plugin.numParameters();
    for (int p = 0; p < numParams; ++p) {
        // Skip output bus routing params (could cause out-of-bounds bus access)
        if (p == kP_Output || p == kP_OutputMode ||
            p == kP_LinFMInput || p == kP_V8Input ||
            p == kP_MorphInput || p == kP_HarmonicsInput) continue;

        const auto* param = &plugin.algorithm()->parameters[p];
        // Sweep min → max in steps
        int range = param->max - param->min;
        int step = std::max(1, range / 10);
        for (int val = param->min; val <= param->max; val += step) {
            plugin.setParameter(p, val);
            plugin.step(BLOCK_SIZE);
        }
    }
    ASSERT_TRUE(true, "survived param sweep");
    TEST_PASS();
}

TestResult test_detune_models() {
    TEST_BEGIN("All detune models produce audio");
    // DetuneType 0-7: Tri bell, TR-808, Hammond, Piano, Bell, Marimba, Bass drum, Harmonics
    for (int model = 0; model <= 7; ++model) {
        PluginInstance plugin;
        ASSERT_TRUE(createPlugin(plugin), "plugin created");
        plugin.setParameter(kP_Detune, 5000);       // significant detune
        plugin.setParameter(kP_DetuneType, model);

        float peakTotal = 0.0f;
        for (int b = 0; b < blocksFor(0.05f); ++b) {
            float* bus = plugin.step(BLOCK_SIZE);
            float* out = bus + (OUTPUT_BUS - 1) * BLOCK_SIZE;
            peakTotal = std::max(peakTotal, PluginInstance::peak(out, BLOCK_SIZE));
        }
        ASSERT_GT(peakTotal, 0.001f,
            std::string("detune model ") + std::to_string(model) + " should produce audio");
    }
    TEST_PASS();
}

TestResult test_wav_capture() {
    TEST_BEGIN("WAV capture (writes bin/lofi_osc_output.wav)");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    // Middle C, moderate gain, saw waveform with some detune
    plugin.setParameter(kP_Waveform, 3);    // Sawtooth
    plugin.setParameter(kP_Gain, 3000);
    plugin.setParameter(kP_Detune, 3000);
    plugin.setParameter(kP_DetuneType, 1);  // TR-808

    WavWriter wav("bin/lofi_osc_output.wav", SAMPLE_RATE, 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");
    float peak = renderToWav(plugin, wav, 2.0f);
    wav.close();

    ASSERT_GT(peak, 0.01f, "WAV capture has audio");
    TEST_PASS();
}

// =========================================================================
// Golden Hash WAV generators — each writes a deterministic WAV to bin/
// =========================================================================

TestResult test_golden_default() {
    TEST_BEGIN("Golden: default config (sine, middle C)");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");

    WavWriter wav("bin/golden_default.wav", SAMPLE_RATE, 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");
    float peak = renderToWav(plugin, wav, 1.0f);
    wav.close();
    ASSERT_GT(peak, 0.001f, "has audio");
    TEST_PASS();
}

TestResult test_golden_waveforms() {
    TEST_BEGIN("Golden: all 5 waveforms");
    const char* names[] = {"sine", "square", "triangle", "saw", "morph"};

    for (int w = 0; w <= 4; ++w) {
        PluginInstance plugin;
        ASSERT_TRUE(createPlugin(plugin), "plugin created");
        plugin.setParameter(kP_Waveform, w);

        std::string path = std::string("bin/golden_wave_") + names[w] + ".wav";
        WavWriter wav(path.c_str(), SAMPLE_RATE, 1);
        ASSERT_TRUE(wav.isOpen(), (std::string("opened ") + path).c_str());
        float peak = renderToWav(plugin, wav, 1.0f);
        wav.close();
        ASSERT_GT(peak, 0.001f,
            (std::string(names[w]) + " has audio").c_str());
    }
    TEST_PASS();
}

TestResult test_golden_detune_models() {
    TEST_BEGIN("Golden: detune models");
    const char* names[] = {
        "random", "tr808", "hammond", "piano",
        "bell", "marimba", "bassdrum", "harmonics"
    };

    for (int m = 0; m <= 7; ++m) {
        PluginInstance plugin;
        ASSERT_TRUE(createPlugin(plugin), "plugin created");
        plugin.setParameter(kP_Waveform, 3);     // Saw
        plugin.setParameter(kP_Detune, 5000);
        plugin.setParameter(kP_DetuneType, m);

        std::string path = std::string("bin/golden_detune_") + names[m] + ".wav";
        WavWriter wav(path.c_str(), SAMPLE_RATE, 1);
        ASSERT_TRUE(wav.isOpen(), (std::string("opened ") + path).c_str());
        float peak = renderToWav(plugin, wav, 1.0f);
        wav.close();
        ASSERT_GT(peak, 0.001f,
            (std::string(names[m]) + " has audio").c_str());
    }
    TEST_PASS();
}

TestResult test_golden_morph_sweep() {
    TEST_BEGIN("Golden: morph sweep");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");
    plugin.setParameter(kP_Waveform, 4);  // Morph

    WavWriter wav("bin/golden_morph_sweep.wav", SAMPLE_RATE, 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Sweep morph from 0 to 1000 over 2 seconds
    int totalBlocks = blocksFor(2.0f);
    for (int b = 0; b < totalBlocks; ++b) {
        int morphVal = (b * 1000) / totalBlocks;
        plugin.setParameter(kP_OscMorph, morphVal);
        plugin.step(BLOCK_SIZE);
        wav.writeMono(plugin.getBus(OUTPUT_BUS - 1, BLOCK_SIZE), BLOCK_SIZE);
    }
    wav.close();
    ASSERT_TRUE(true, "morph sweep WAV written");
    TEST_PASS();
}

TestResult test_golden_harmonics() {
    TEST_BEGIN("Golden: harmonics modulation");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");
    plugin.setParameter(kP_Waveform, 0);      // Sine
    plugin.setParameter(kP_Detune, 3000);
    plugin.setParameter(kP_DetuneType, 2);    // Hammond

    WavWriter wav("bin/golden_harmonics.wav", SAMPLE_RATE, 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // Sweep harmonics from full (1000) to zero over 2 seconds
    int totalBlocks = blocksFor(2.0f);
    for (int b = 0; b < totalBlocks; ++b) {
        int harmVal = 1000 - (b * 1000) / totalBlocks;
        plugin.setParameter(kP_Harmonics, harmVal);
        plugin.step(BLOCK_SIZE);
        wav.writeMono(plugin.getBus(OUTPUT_BUS - 1, BLOCK_SIZE), BLOCK_SIZE);
    }
    wav.close();
    ASSERT_TRUE(true, "harmonics WAV written");
    TEST_PASS();
}

TestResult test_golden_pitch_range() {
    TEST_BEGIN("Golden: pitch range (low/mid/high)");
    int semitones[] = { -24, 0, 24 };
    const char* names[] = { "low", "mid", "high" };

    for (int i = 0; i < 3; ++i) {
        PluginInstance plugin;
        ASSERT_TRUE(createPlugin(plugin), "plugin created");
        plugin.setParameter(kP_Waveform, 3);  // Saw
        plugin.setParameter(kP_OscSemi, semitones[i]);

        std::string path = std::string("bin/golden_pitch_") + names[i] + ".wav";
        WavWriter wav(path.c_str(), SAMPLE_RATE, 1);
        ASSERT_TRUE(wav.isOpen(), (std::string("opened ") + path).c_str());
        float peak = renderToWav(plugin, wav, 1.0f);
        wav.close();
        ASSERT_GT(peak, 0.001f,
            (std::string(names[i]) + " has audio").c_str());
    }
    TEST_PASS();
}

// =========================================================================
// Golden Hash WAV generators — CV modulation at sample rate
// =========================================================================

// Bus allocation for CV tests:
//   Bus 1 (index 0) = output
//   Bus 2 (index 1) = CV input
static constexpr int CV_BUS = 2;   // 1-based parameter value
static constexpr int CV_BUS_IDX = 1; // 0-based for fillBus

/// Generate a block of sine CV at a given frequency and amplitude (volts).
static void generateSineCv(float* buf, int numSamples, float freqHz,
                           float amplitude, int blockIndex) {
    int sampleOffset = blockIndex * numSamples;
    for (int i = 0; i < numSamples; ++i) {
        float t = (float)(sampleOffset + i) / (float)SAMPLE_RATE;
        buf[i] = amplitude * sinf(2.0f * 3.14159265f * freqHz * t);
    }
}

/// Generate a block of ramp CV (0 to amplitude over totalBlocks).
static void generateRampCv(float* buf, int numSamples, float amplitude,
                           int blockIndex, int totalBlocks) {
    int sampleOffset = blockIndex * numSamples;
    int totalSamples = totalBlocks * numSamples;
    for (int i = 0; i < numSamples; ++i) {
        buf[i] = amplitude * (float)(sampleOffset + i) / (float)totalSamples;
    }
}

TestResult test_golden_cv_fm() {
    TEST_BEGIN("Golden: linear FM CV modulation");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");
    plugin.setParameter(kP_Waveform, 0);      // Sine carrier
    plugin.setParameter(kP_FmDepth, 5000);    // Significant FM depth
    plugin.setParameter(kP_LinFMInput, CV_BUS);

    WavWriter wav("bin/golden_cv_fm.wav", SAMPLE_RATE, 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    float cvBlock[BLOCK_SIZE];
    int totalBlocks = blocksFor(1.0f);
    float peakAll = 0.0f;
    for (int b = 0; b < totalBlocks; ++b) {
        // 110 Hz sine modulator at ±2V
        generateSineCv(cvBlock, BLOCK_SIZE, 110.0f, 2.0f, b);
        plugin.prepareStep(BLOCK_SIZE);
        plugin.fillBus(CV_BUS_IDX, cvBlock, BLOCK_SIZE);
        plugin.executeStep(BLOCK_SIZE);
        const float* out = plugin.getBus(OUTPUT_BUS - 1, BLOCK_SIZE);
        wav.writeMono(out, BLOCK_SIZE);
        float p = PluginInstance::peak(out, BLOCK_SIZE);
        if (p > peakAll) peakAll = p;
    }
    wav.close();
    ASSERT_GT(peakAll, 0.001f, "FM-modulated output has audio");
    TEST_PASS();
}

TestResult test_golden_cv_voct() {
    TEST_BEGIN("Golden: V/Oct CV pitch tracking");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");
    plugin.setParameter(kP_Waveform, 3);      // Saw
    plugin.setParameter(kP_V8Input, CV_BUS);

    WavWriter wav("bin/golden_cv_voct.wav", SAMPLE_RATE, 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    float cvBlock[BLOCK_SIZE];
    int totalBlocks = blocksFor(2.0f);
    float peakAll = 0.0f;
    for (int b = 0; b < totalBlocks; ++b) {
        // Ramp from 0V to +2V over 2 seconds (2 octaves up)
        generateRampCv(cvBlock, BLOCK_SIZE, 2.0f, b, totalBlocks);
        plugin.prepareStep(BLOCK_SIZE);
        plugin.fillBus(CV_BUS_IDX, cvBlock, BLOCK_SIZE);
        plugin.executeStep(BLOCK_SIZE);
        const float* out = plugin.getBus(OUTPUT_BUS - 1, BLOCK_SIZE);
        wav.writeMono(out, BLOCK_SIZE);
        float p = PluginInstance::peak(out, BLOCK_SIZE);
        if (p > peakAll) peakAll = p;
    }
    wav.close();
    ASSERT_GT(peakAll, 0.001f, "V/Oct modulated output has audio");
    TEST_PASS();
}

TestResult test_golden_cv_morph() {
    TEST_BEGIN("Golden: Morph CV modulation");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");
    plugin.setParameter(kP_Waveform, 4);      // Morph waveform
    plugin.setParameter(kP_MorphModDepth, 1000); // Full depth
    plugin.setParameter(kP_MorphInput, CV_BUS);

    WavWriter wav("bin/golden_cv_morph.wav", SAMPLE_RATE, 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    float cvBlock[BLOCK_SIZE];
    int totalBlocks = blocksFor(2.0f);
    float peakAll = 0.0f;
    for (int b = 0; b < totalBlocks; ++b) {
        // Slow 1 Hz sine, ±3V — sweeps morph over time
        generateSineCv(cvBlock, BLOCK_SIZE, 1.0f, 3.0f, b);
        plugin.prepareStep(BLOCK_SIZE);
        plugin.fillBus(CV_BUS_IDX, cvBlock, BLOCK_SIZE);
        plugin.executeStep(BLOCK_SIZE);
        const float* out = plugin.getBus(OUTPUT_BUS - 1, BLOCK_SIZE);
        wav.writeMono(out, BLOCK_SIZE);
        float p = PluginInstance::peak(out, BLOCK_SIZE);
        if (p > peakAll) peakAll = p;
    }
    wav.close();
    ASSERT_GT(peakAll, 0.001f, "Morph-modulated output has audio");
    TEST_PASS();
}

TestResult test_golden_cv_harmonics() {
    TEST_BEGIN("Golden: Harmonics CV modulation");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");
    plugin.setParameter(kP_Waveform, 0);      // Sine
    plugin.setParameter(kP_Detune, 4000);
    plugin.setParameter(kP_DetuneType, 2);    // Hammond partials
    plugin.setParameter(kP_HarmonicsInput, CV_BUS);

    WavWriter wav("bin/golden_cv_harmonics.wav", SAMPLE_RATE, 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    float cvBlock[BLOCK_SIZE];
    int totalBlocks = blocksFor(2.0f);
    float peakAll = 0.0f;
    for (int b = 0; b < totalBlocks; ++b) {
        // Slow 0.5 Hz triangle-ish via sine, ±5V — sweeps harmonics filter
        generateSineCv(cvBlock, BLOCK_SIZE, 0.5f, 5.0f, b);
        plugin.prepareStep(BLOCK_SIZE);
        plugin.fillBus(CV_BUS_IDX, cvBlock, BLOCK_SIZE);
        plugin.executeStep(BLOCK_SIZE);
        const float* out = plugin.getBus(OUTPUT_BUS - 1, BLOCK_SIZE);
        wav.writeMono(out, BLOCK_SIZE);
        float p = PluginInstance::peak(out, BLOCK_SIZE);
        if (p > peakAll) peakAll = p;
    }
    wav.close();
    ASSERT_GT(peakAll, 0.001f, "Harmonics-modulated output has audio");
    TEST_PASS();
}

TestResult test_golden_cv_all_inputs() {
    TEST_BEGIN("Golden: all 4 CV inputs simultaneously");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin created");
    plugin.setParameter(kP_Waveform, 4);       // Morph
    plugin.setParameter(kP_FmDepth, 2000);
    plugin.setParameter(kP_Detune, 3000);
    plugin.setParameter(kP_DetuneType, 1);     // TR-808
    plugin.setParameter(kP_MorphModDepth, 1000);

    // Each CV input on a different bus (2-5, 1-based)
    plugin.setParameter(kP_LinFMInput, 2);
    plugin.setParameter(kP_V8Input, 3);
    plugin.setParameter(kP_MorphInput, 4);
    plugin.setParameter(kP_HarmonicsInput, 5);

    WavWriter wav("bin/golden_cv_all.wav", SAMPLE_RATE, 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    float fmBuf[BLOCK_SIZE], voctBuf[BLOCK_SIZE];
    float morphBuf[BLOCK_SIZE], harmBuf[BLOCK_SIZE];
    int totalBlocks = blocksFor(2.0f);
    float peakAll = 0.0f;
    for (int b = 0; b < totalBlocks; ++b) {
        generateSineCv(fmBuf,    BLOCK_SIZE, 80.0f,  1.5f, b);  // FM: 80 Hz
        generateRampCv(voctBuf,  BLOCK_SIZE, 1.0f, b, totalBlocks); // V/Oct: +1V ramp
        generateSineCv(morphBuf, BLOCK_SIZE, 0.8f,  3.0f, b);  // Morph: 0.8 Hz
        generateSineCv(harmBuf,  BLOCK_SIZE, 0.3f,  4.0f, b);  // Harmonics: 0.3 Hz

        plugin.prepareStep(BLOCK_SIZE);
        plugin.fillBus(1, fmBuf,    BLOCK_SIZE);  // bus index 1 = bus 2
        plugin.fillBus(2, voctBuf,  BLOCK_SIZE);  // bus index 2 = bus 3
        plugin.fillBus(3, morphBuf, BLOCK_SIZE);  // bus index 3 = bus 4
        plugin.fillBus(4, harmBuf,  BLOCK_SIZE);  // bus index 4 = bus 5
        plugin.executeStep(BLOCK_SIZE);
        const float* out = plugin.getBus(OUTPUT_BUS - 1, BLOCK_SIZE);
        wav.writeMono(out, BLOCK_SIZE);
        float p = PluginInstance::peak(out, BLOCK_SIZE);
        if (p > peakAll) peakAll = p;
    }
    wav.close();
    ASSERT_GT(peakAll, 0.001f, "all-CV-modulated output has audio");
    TEST_PASS();
}

// =========================================================================
// Golden hash verification — SHA-256 regression test
// =========================================================================

TestResult test_golden_wav_hashes() {
    TEST_BEGIN("Golden WAV hashes (SHA-256 regression)");

    const char* goldenPath = "tests/golden_hashes.txt";

    // --- Read the golden file ---
    FILE* f = fopen(goldenPath, "r");
    ASSERT_TRUE(f != nullptr, "tests/golden_hashes.txt exists");

    struct Line {
        std::string raw;
        std::string hash;
        std::string file;
        std::string actual;
        bool isEntry = false;
    };

    std::vector<Line> lines;
    char buf[512];
    while (fgets(buf, sizeof(buf), f)) {
        Line ln;
        ln.raw = buf;
        while (!ln.raw.empty() && (ln.raw.back() == '\n' || ln.raw.back() == '\r'))
            ln.raw.pop_back();

        if (ln.raw.empty() || ln.raw[0] == '#') {
            lines.push_back(ln);
            continue;
        }

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
            printf("    REGEN  %-40s %s\n", ln.file.c_str(), ln.actual.c_str());
            ++regenerated;
        } else if (ln.actual != ln.hash) {
            printf("    MISMATCH %-38s\n      expected: %s\n      actual:   %s\n",
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

// -------------------------------------------------------------------------
// Main
// -------------------------------------------------------------------------
int main() {
    NtTestHarness::setSampleRate(SAMPLE_RATE);
    NtTestHarness::setMaxFrames(BLOCK_SIZE);

    return TestRunner::run({
        test_plugin_loads,
        test_produces_audio,
        test_silence_when_gain_zero,
        test_all_waveforms_produce_audio,
        test_sweep_params_no_crash,
        test_detune_models,
        test_wav_capture,
        // Golden WAV generators
        test_golden_default,
        test_golden_waveforms,
        test_golden_detune_models,
        test_golden_morph_sweep,
        test_golden_harmonics,
        test_golden_pitch_range,
        // Golden CV modulation generators
        test_golden_cv_fm,
        test_golden_cv_voct,
        test_golden_cv_morph,
        test_golden_cv_harmonics,
        test_golden_cv_all_inputs,
        // Golden hash verification (must run after generators)
        test_golden_wav_hashes,
    });
}
