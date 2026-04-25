// =============================================================================
// NoiseBouquet/tests/test_integration.cpp
// =============================================================================
// Host-side integration tests for the NoiseBouquet plugin.
//
// Modelled on PolyLofi/tests/test_integration.cpp:
//   - test_framework.h     (TEST_BEGIN / ASSERT_*)
//   - plugin_harness.h     (PluginInstance lifecycle)
//   - wav_writer.h         (16-bit PCM WAV capture for offline listening)
//   - sha256.h             (regression: golden hashes for rendered WAVs)
//
// Run:
//   make test          # build bin/tests
//   make test-run      # build + run + convert any .pgm screen dumps
// =============================================================================

#include "../../test_harness/test_framework.h"
#include "../../test_harness/plugin_harness.h"
#include "../../test_harness/wav_writer.h"
#include "../../test_harness/sha256.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// Constants (must match NoiseBouquet.cpp)
// -----------------------------------------------------------------------------
static constexpr int OUTPUT_BUS_1BASED = 13; // matches kParamOut default
static constexpr int OUTPUT_BUS        = OUTPUT_BUS_1BASED - 1; // 0-based
static constexpr int BLOCK_SIZE        = 64;
static constexpr int kBank1            = 1;
static constexpr int kBank2            = 2;
static constexpr int kBank3            = 3;
static constexpr int kBank4            = 4;

// Mirror the kParam* enum from NoiseBouquet.cpp.
enum {
    kParamOut,
    kParamOutMode,
    kParamBank,
    kParamProgram,
    kParamX,
    kParamY,
    kParamGain,
};

// Program layout from PluginRegistry.hpp.
static constexpr int kProg_radioOhNo          = 1;
static constexpr int kProg_Rwalk_SineFMFlange = 2;
static constexpr int kProg_xModRingSqr        = 3;
static constexpr int kProg_XModRingSine       = 4;
static constexpr int kProg_CrossModRing       = 5;
static constexpr int kProg_resonoise          = 6;
static constexpr int kProg_grainGlitch        = 7;
static constexpr int kProg_grainGlitchII      = 8;
static constexpr int kProg_grainGlitchIII     = 9;
static constexpr int kProg_basurilla          = 10;

static constexpr int kProg_clusterSaw       = 1;
static constexpr int kProg_pwCluster        = 2;
static constexpr int kProg_crCluster2       = 3;
static constexpr int kProg_sineFMcluster    = 4;
static constexpr int kProg_TriFMcluster     = 5;
static constexpr int kProg_PrimeCluster     = 6;
static constexpr int kProg_PrimeCnoise      = 7;
static constexpr int kProg_FibonacciCluster = 8;
static constexpr int kProg_partialCluster   = 9;
static constexpr int kProg_phasingCluster   = 10;

static constexpr int kProg_BasuraTotal      = 1;
static constexpr int kProg_Atari            = 2;
static constexpr int kProg_WalkingFilomena  = 3;
static constexpr int kProg_S_H              = 4;
static constexpr int kProg_arrayOnTheRocks  = 5;
static constexpr int kProg_existencelsPain  = 6;
static constexpr int kProg_whoKnows         = 7;
static constexpr int kProg_satanWorkout     = 8;
static constexpr int kProg_Rwalk_BitCrushPW = 9;
static constexpr int kProg_Rwalk_LFree      = 10;

static constexpr int kProg_TestPlugin = 1;
static constexpr int kProg_WhiteNoise = 2;
static constexpr int kProg_TeensyAlt  = 3;

// M3 effect-layer tests live in tests/test_effect_units.cpp and are linked into
// the same runner so the existing golden hash check can cover both plugin WAVs
// and effect-unit WAVs in one pass.
TestResult test_effect_bitcrusher_wav();
TestResult test_effect_combine_wav();
TestResult test_effect_multiply_wav();
TestResult test_effect_wavefolder_wav();

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

/// Construct a fresh PluginInstance with the WhiteNoise factory loaded.
/// Note: the Teensy white-noise unit seeds from a global instance counter
/// that increments per construction. The order of tests in main() is fixed,
/// so seeds (and therefore golden-hash WAVs) are deterministic across runs
/// — but adding/removing/reordering tests above the WAV captures will
/// change them and require regenerating the golden hashes.
static bool createPlugin(PluginInstance& plugin) {
    if (!plugin.load(0)) return false;
    plugin.initStatic();
    return plugin.construct();
}

static int blocksFor(float seconds) {
    return static_cast<int>(NtTestHarness::getSampleRate() * seconds / BLOCK_SIZE);
}

/// Render `seconds` of audio from `plugin` to `wav`, returning peak abs value.
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

static int countZeroCrossings(const float* samples, int count) {
    int crossings = 0;
    int prevSign = 0;
    for (int i = 0; i < count; ++i) {
        const float sample = samples[i];
        const int sign = (sample > 0.0f) - (sample < 0.0f);
        if (sign == 0) continue;
        if (prevSign != 0 && sign != prevSign) ++crossings;
        prevSign = sign;
    }
    return crossings;
}

// =============================================================================
// Tests
// =============================================================================

TestResult test_plugin_loads() {
    TEST_BEGIN("plugin loads and exposes 7 parameters");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin constructed");
    ASSERT_NOT_NULL(plugin.factory(), "factory present");
    ASSERT_TRUE(std::strcmp(plugin.name(), "Noise Bouquet") == 0,
                "factory name = 'Noise Bouquet'");
    ASSERT_EQ(plugin.numParameters(), 7, "7 parameters defined");
    TEST_PASS();
}

TestResult test_default_construct_audible() {
    TEST_BEGIN("white noise audible at default params");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin constructed");

    // Burn a few blocks for the ring buffer to fill, then measure.
    for (int i = 0; i < 8; ++i) plugin.step(BLOCK_SIZE);

    float maxPeak = 0.0f;
    double rmsAcc = 0.0;
    int blocks = 64;
    for (int i = 0; i < blocks; ++i) {
        plugin.step(BLOCK_SIZE);
        const float* bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
        float p = PluginInstance::peak(bus, BLOCK_SIZE);
        if (p > maxPeak) maxPeak = p;
        float r = PluginInstance::rms(bus, BLOCK_SIZE);
        rmsAcc += (double)r * r;
    }
    float meanRms = (float)std::sqrt(rmsAcc / blocks);

    // White noise at gain=1.0 should hit ~±1.0 with RMS ~0.55.
    ASSERT_GT(maxPeak, 0.1f,  "peak above noise floor");
    ASSERT_GT(meanRms, 0.2f,  "RMS in expected band (lower)");
    ASSERT_LT(meanRms, 0.9f,  "RMS in expected band (upper)");
    TEST_PASS();
}

TestResult test_gain_zero_silent() {
    TEST_BEGIN("Gain=0 produces silence");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin constructed");
    plugin.setParameter(kParamGain, 0);

    // Burn a few blocks then measure.
    for (int i = 0; i < 8; ++i) plugin.step(BLOCK_SIZE);

    float maxPeak = 0.0f;
    for (int i = 0; i < 16; ++i) {
        plugin.step(BLOCK_SIZE);
        const float* bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
        float p = PluginInstance::peak(bus, BLOCK_SIZE);
        if (p > maxPeak) maxPeak = p;
    }
    ASSERT_LT(maxPeak, 1e-6f, "output is silent at gain=0");
    TEST_PASS();
}

TestResult test_replace_mode_overwrites_bus() {
    TEST_BEGIN("Out mode = Replace overwrites pre-existing bus content");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin constructed");
    plugin.setParameter(kParamOutMode, 1); // 1 = Replace

    // Burn a few blocks first so we measure steady-state.
    for (int i = 0; i < 8; ++i) plugin.step(BLOCK_SIZE);

    // Pre-fill output bus with a known DC value, then run step.
    std::vector<float> dc(BLOCK_SIZE, 1.0f);
    plugin.prepareStep(BLOCK_SIZE);
    plugin.fillBus(OUTPUT_BUS, dc.data(), BLOCK_SIZE);
    plugin.executeStep(BLOCK_SIZE);

    const float* bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
    // In replace mode the DC offset should be gone — mean should be ~0.
    double mean = 0.0;
    for (int i = 0; i < BLOCK_SIZE; ++i) mean += bus[i];
    mean /= BLOCK_SIZE;
    ASSERT_LT(std::fabs((float)mean), 0.5f, "DC pre-fill replaced (not summed)");
    TEST_PASS();
}

TestResult test_add_mode_sums_with_bus() {
    TEST_BEGIN("Out mode = Add sums with pre-existing bus content");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin constructed");
    plugin.setParameter(kParamOutMode, 0); // 0 = Add

    for (int i = 0; i < 8; ++i) plugin.step(BLOCK_SIZE);

    // Pre-fill bus with DC=1.0; in add mode mean ≈ 1.0 + ~0.
    std::vector<float> dc(BLOCK_SIZE, 1.0f);
    plugin.prepareStep(BLOCK_SIZE);
    plugin.fillBus(OUTPUT_BUS, dc.data(), BLOCK_SIZE);
    plugin.executeStep(BLOCK_SIZE);

    const float* bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
    double mean = 0.0;
    for (int i = 0; i < BLOCK_SIZE; ++i) mean += bus[i];
    mean /= BLOCK_SIZE;
    ASSERT_GT((float)mean, 0.5f, "DC pre-fill summed with noise");
    TEST_PASS();
}

TestResult test_unrouted_no_crash() {
    TEST_BEGIN("Out=0 (unrouted) does not crash and writes nothing");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin constructed");
    plugin.setParameter(kParamOut, 0);

    plugin.prepareStep(BLOCK_SIZE);
    plugin.executeStep(BLOCK_SIZE);
    const float* bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
    float p = PluginInstance::peak(bus, BLOCK_SIZE);
    ASSERT_LT(p, 1e-6f, "default output bus untouched when Out=0");
    TEST_PASS();
}

// -----------------------------------------------------------------------------
// Bank / Program switching
// -----------------------------------------------------------------------------

/// Helper: fully complete the crossfade triggered by a program change so
/// subsequent measurements reflect the new algorithm at unity gain.
static void waitForSwitch(PluginInstance& plugin) {
    // 30 ms fade-out + 30 ms fade-in = 60 ms; round up generously.
    int blocks = (int)(NtTestHarness::getSampleRate() * 0.10f / BLOCK_SIZE);
    for (int i = 0; i < blocks; ++i) plugin.step(BLOCK_SIZE);
}

static void selectProgram(PluginInstance& plugin, int bank, int program) {
    plugin.setParameter(kParamBank, bank);
    plugin.setParameter(kParamProgram, program);
    waitForSwitch(plugin);
}

static TestResult renderProgramWav(const char* testName,
                                   int bank,
                                   int program,
                                   const char* path,
                                   float minPeak) {
    TEST_BEGIN(testName);
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin constructed");
    selectProgram(plugin, bank, program);

    WavWriter wav(path, NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    float peak = renderToWav(plugin, wav, 1.0f);
    wav.close();
    ASSERT_GT(peak, minPeak, "rendered audio is non-silent");
    TEST_PASS();
}

TestResult test_program_switch_to_test_plugin() {
    TEST_BEGIN("Program change -> TestPlugin produces audio after fade");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin constructed");
    plugin.setParameter(kParamProgram, kProg_TestPlugin);
    waitForSwitch(plugin);

    float maxPeak = 0.0f;
    for (int i = 0; i < 32; ++i) {
        plugin.step(BLOCK_SIZE);
        const float* bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
        float p = PluginInstance::peak(bus, BLOCK_SIZE);
        if (p > maxPeak) maxPeak = p;
    }
    ASSERT_GT(maxPeak, 0.05f, "TestPlugin audible after switch");
    TEST_PASS();
}

TestResult test_program_switch_to_teensy_alt() {
    TEST_BEGIN("Program change -> TeensyAlt produces audio after fade");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin constructed");
    plugin.setParameter(kParamProgram, kProg_TeensyAlt);
    waitForSwitch(plugin);

    float maxPeak = 0.0f;
    for (int i = 0; i < 32; ++i) {
        plugin.step(BLOCK_SIZE);
        const float* bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
        float p = PluginInstance::peak(bus, BLOCK_SIZE);
        if (p > maxPeak) maxPeak = p;
    }
    ASSERT_GT(maxPeak, 0.01f, "TeensyAlt audible after switch");
    TEST_PASS();
}

TestResult test_xy_changes_are_smoothed() {
    TEST_BEGIN("X/Y parameter changes ramp over multiple blocks");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin constructed");
    selectProgram(plugin, kBank4, kProg_TestPlugin);

    // Hold waveform selection steady on the sine branch so zero-crossing
    // count tracks the frequency change from X.
    plugin.setParameter(kParamY, 6000);
    for (int i = 0; i < 12; ++i) plugin.step(BLOCK_SIZE);

    // Drive X to zero and let the smoother settle near DC.
    plugin.setParameter(kParamX, 0);
    for (int i = 0; i < 40; ++i) plugin.step(BLOCK_SIZE);

    // Jump to max X. With smoothing, the first block should still be much
    // lower in frequency than the settled state a little later.
    plugin.setParameter(kParamX, 10000);
    plugin.step(BLOCK_SIZE);
    const float* firstBlock = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
    const int firstCrossings = countZeroCrossings(firstBlock, BLOCK_SIZE);

    for (int i = 0; i < 20; ++i) plugin.step(BLOCK_SIZE);
    const float* settledBlock = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
    const int settledCrossings = countZeroCrossings(settledBlock, BLOCK_SIZE);

    ASSERT_GT(settledCrossings, 8, "settled TestPlugin block reaches high frequency");
    ASSERT_LT(firstCrossings, settledCrossings, "first block after X jump is still smoothed");
    TEST_PASS();
}

TestResult test_bank1_first_slot_audible() {
    TEST_BEGIN("Bank 1 / Program 1 renders audio after fade");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin constructed");
    selectProgram(plugin, kBank1, kProg_radioOhNo);

    float maxPeak = 0.0f;
    for (int i = 0; i < 16; ++i) {
        plugin.step(BLOCK_SIZE);
        const float* bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
        float p = PluginInstance::peak(bus, BLOCK_SIZE);
        if (p > maxPeak) maxPeak = p;
    }
    ASSERT_GT(maxPeak, 0.01f, "Bank 1 first slot is audible");
    TEST_PASS();
}

TestResult test_parameter_string_program_names() {
    TEST_BEGIN("parameterString returns Bank/Program names");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin constructed");

    char buf[64];
    auto str = [&](int paramIdx, int value) -> std::string {
        std::memset(buf, 0, sizeof(buf));
        int n = plugin.factory()->parameterString(plugin.algorithm(),
                                                  paramIdx, value, buf);
        return (n > 0) ? std::string(buf) : std::string();
    };

    // Bank label
    ASSERT_TRUE(str(kParamBank, 4) == "Bank 4", "Bank 4 label");
    ASSERT_TRUE(str(kParamBank, 2) == "Bank 2", "Bank 2 label");
    ASSERT_TRUE(str(kParamBank, 1) == "Bank 1", "Bank 1 label");

    // Program names need the current Bank in v[]; createPlugin defaults to Bank 4.
    ASSERT_TRUE(str(kParamProgram, kProg_TestPlugin) == "TestPlugin",
                "Program 1 = TestPlugin");
    ASSERT_TRUE(str(kParamProgram, kProg_WhiteNoise) == "WhiteNoise",
                "Program 2 = WhiteNoise");
    ASSERT_TRUE(str(kParamProgram, kProg_TeensyAlt) == "TeensyAlt",
                "Program 3 = TeensyAlt");
    ASSERT_TRUE(str(kParamProgram, 5) == "--",
                "empty Bank-4 slot shows '--'");

    plugin.setParameter(kParamBank, kBank2);
    ASSERT_TRUE(str(kParamProgram, kProg_clusterSaw) == "clusterSaw",
                "Bank 2 / Program 1 = clusterSaw");
    ASSERT_TRUE(str(kParamProgram, kProg_PrimeCluster) == "PrimeCluster",
                "Bank 2 / Program 6 = PrimeCluster");
    ASSERT_TRUE(str(kParamProgram, kProg_phasingCluster) == "phasingCluster",
                "Bank 2 / Program 10 = phasingCluster");

    plugin.setParameter(kParamBank, kBank3);
    ASSERT_TRUE(str(kParamProgram, kProg_BasuraTotal) == "BasuraTotal",
                "Bank 3 / Program 1 = BasuraTotal");
    ASSERT_TRUE(str(kParamProgram, kProg_arrayOnTheRocks) == "arrayOnTheRocks",
                "Bank 3 / Program 5 = arrayOnTheRocks");
    ASSERT_TRUE(str(kParamProgram, kProg_Rwalk_LFree) == "Rwalk_LFree",
                "Bank 3 / Program 10 = Rwalk_LFree");

    plugin.setParameter(kParamBank, kBank1);
    ASSERT_TRUE(str(kParamProgram, kProg_radioOhNo) == "radioOhNo",
                "Bank 1 / Program 1 = radioOhNo");
    ASSERT_TRUE(str(kParamProgram, kProg_resonoise) == "resonoise",
                "Bank 1 / Program 6 = resonoise");
    ASSERT_TRUE(str(kParamProgram, kProg_basurilla) == "basurilla",
                "Bank 1 / Program 10 = basurilla");

    TEST_PASS();
}

// -----------------------------------------------------------------------------
// WAV captures (golden-hash regression targets)
// -----------------------------------------------------------------------------
//
// All WAV-rendering tests run BEFORE any other test (see main()). This pins
// the global noise seeds (AudioSynthNoiseWhite::instance_count and the static
// teensy::seed used by SAMPLE_HOLD) so that adding new unit tests later does
// NOT invalidate the golden hashes.

TestResult test_white_noise_default_wav() {
    TEST_BEGIN("Render 1.0s of WhiteNoise (default params) -> bin/white_noise_default.wav");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin constructed");

    WavWriter wav("bin/white_noise_default.wav",
                  NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    float peak = renderToWav(plugin, wav, 1.0f);
    wav.close();
    ASSERT_GT(peak, 0.1f, "rendered audio is non-silent");
    TEST_PASS();
}

TestResult test_white_noise_half_gain_wav() {
    TEST_BEGIN("Render 1.0s of WhiteNoise (Gain=50%) -> bin/white_noise_half_gain.wav");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin constructed");
    plugin.setParameter(kParamGain, 500); // 0.5

    WavWriter wav("bin/white_noise_half_gain.wav",
                  NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    float peak = renderToWav(plugin, wav, 1.0f);
    wav.close();
    ASSERT_GT(peak, 0.05f, "rendered audio is non-silent");
    ASSERT_LT(peak, 0.7f,  "rendered audio respects half-gain");
    TEST_PASS();
}

TestResult test_test_plugin_wav() {
    TEST_BEGIN("Switch to TestPlugin and render 1.0s -> bin/test_plugin.wav");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin constructed");
    selectProgram(plugin, kBank4, kProg_TestPlugin);

    WavWriter wav("bin/test_plugin.wav",
                  NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    // The first ~60 ms of the WAV is the fade-out of WhiteNoise into the
    // fade-in of TestPlugin — deterministic and useful for ear-checking the
    // crossfade.
    float peak = renderToWav(plugin, wav, 1.0f);
    wav.close();
    ASSERT_GT(peak, 0.05f, "TestPlugin produces audio after switch");
    TEST_PASS();
}

TestResult test_teensy_alt_wav() {
    TEST_BEGIN("Switch to TeensyAlt and render 1.0s -> bin/teensy_alt.wav");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin constructed");
    selectProgram(plugin, kBank4, kProg_TeensyAlt);

    WavWriter wav("bin/teensy_alt.wav",
                  NtTestHarness::getSampleRate(), 1);
    ASSERT_TRUE(wav.isOpen(), "WAV file opened");

    float peak = renderToWav(plugin, wav, 1.0f);
    wav.close();
    ASSERT_GT(peak, 0.01f, "TeensyAlt produces audio after switch");
    TEST_PASS();
}

TestResult test_radio_oh_no_wav() {
    return renderProgramWav("Switch to radioOhNo and render 1.0s -> bin/radio_oh_no.wav",
                            kBank1, kProg_radioOhNo, "bin/radio_oh_no.wav", 0.01f);
}

TestResult test_rwalk_sine_fm_flange_wav() {
    return renderProgramWav("Switch to Rwalk_SineFMFlange and render 1.0s -> bin/rwalk_sine_fm_flange.wav",
                            kBank1, kProg_Rwalk_SineFMFlange, "bin/rwalk_sine_fm_flange.wav", 0.01f);
}

TestResult test_xmod_ring_sqr_wav() {
    return renderProgramWav("Switch to xModRingSqr and render 1.0s -> bin/xmod_ring_sqr.wav",
                            kBank1, kProg_xModRingSqr, "bin/xmod_ring_sqr.wav", 0.01f);
}

TestResult test_xmod_ring_sine_wav() {
    return renderProgramWav("Switch to XModRingSine and render 1.0s -> bin/xmod_ring_sine.wav",
                            kBank1, kProg_XModRingSine, "bin/xmod_ring_sine.wav", 0.01f);
}

TestResult test_cross_mod_ring_wav() {
    return renderProgramWav("Switch to CrossModRing and render 1.0s -> bin/cross_mod_ring.wav",
                            kBank1, kProg_CrossModRing, "bin/cross_mod_ring.wav", 0.01f);
}

TestResult test_resonoise_wav() {
    return renderProgramWav("Switch to resonoise and render 1.0s -> bin/resonoise.wav",
                            kBank1, kProg_resonoise, "bin/resonoise.wav", 0.01f);
}

TestResult test_grain_glitch_wav() {
    return renderProgramWav("Switch to grainGlitch and render 1.0s -> bin/grain_glitch.wav",
                            kBank1, kProg_grainGlitch, "bin/grain_glitch.wav", 0.01f);
}

TestResult test_grain_glitch_ii_wav() {
    return renderProgramWav("Switch to grainGlitchII and render 1.0s -> bin/grain_glitch_ii.wav",
                            kBank1, kProg_grainGlitchII, "bin/grain_glitch_ii.wav", 0.01f);
}

TestResult test_grain_glitch_iii_wav() {
    return renderProgramWav("Switch to grainGlitchIII and render 1.0s -> bin/grain_glitch_iii.wav",
                            kBank1, kProg_grainGlitchIII, "bin/grain_glitch_iii.wav", 0.01f);
}

TestResult test_basurilla_wav() {
    return renderProgramWav("Switch to basurilla and render 1.0s -> bin/basurilla.wav",
                            kBank1, kProg_basurilla, "bin/basurilla.wav", 0.01f);
}

TestResult test_cluster_saw_wav() {
    return renderProgramWav("Switch to clusterSaw and render 1.0s -> bin/cluster_saw.wav",
                            kBank2, kProg_clusterSaw, "bin/cluster_saw.wav", 0.01f);
}

TestResult test_pw_cluster_wav() {
    return renderProgramWav("Switch to pwCluster and render 1.0s -> bin/pw_cluster.wav",
                            kBank2, kProg_pwCluster, "bin/pw_cluster.wav", 0.01f);
}

TestResult test_cr_cluster2_wav() {
    return renderProgramWav("Switch to crCluster2 and render 1.0s -> bin/cr_cluster2.wav",
                            kBank2, kProg_crCluster2, "bin/cr_cluster2.wav", 0.01f);
}

TestResult test_sine_fm_cluster_wav() {
    return renderProgramWav("Switch to sineFMcluster and render 1.0s -> bin/sine_fm_cluster.wav",
                            kBank2, kProg_sineFMcluster, "bin/sine_fm_cluster.wav", 0.01f);
}

TestResult test_tri_fm_cluster_wav() {
    return renderProgramWav("Switch to TriFMcluster and render 1.0s -> bin/tri_fm_cluster.wav",
                            kBank2, kProg_TriFMcluster, "bin/tri_fm_cluster.wav", 0.01f);
}

TestResult test_prime_cluster_wav() {
    return renderProgramWav("Switch to PrimeCluster and render 1.0s -> bin/prime_cluster.wav",
                            kBank2, kProg_PrimeCluster, "bin/prime_cluster.wav", 0.01f);
}

TestResult test_prime_cnoise_wav() {
    return renderProgramWav("Switch to PrimeCnoise and render 1.0s -> bin/prime_cnoise.wav",
                            kBank2, kProg_PrimeCnoise, "bin/prime_cnoise.wav", 0.01f);
}

TestResult test_fibonacci_cluster_wav() {
    return renderProgramWav("Switch to FibonacciCluster and render 1.0s -> bin/fibonacci_cluster.wav",
                            kBank2, kProg_FibonacciCluster, "bin/fibonacci_cluster.wav", 0.01f);
}

TestResult test_partial_cluster_wav() {
    return renderProgramWav("Switch to partialCluster and render 1.0s -> bin/partial_cluster.wav",
                            kBank2, kProg_partialCluster, "bin/partial_cluster.wav", 0.01f);
}

TestResult test_phasing_cluster_wav() {
    return renderProgramWav("Switch to phasingCluster and render 1.0s -> bin/phasing_cluster.wav",
                            kBank2, kProg_phasingCluster, "bin/phasing_cluster.wav", 0.01f);
}

TestResult test_basura_total_wav() {
    return renderProgramWav("Switch to BasuraTotal and render 1.0s -> bin/basura_total.wav",
                            kBank3, kProg_BasuraTotal, "bin/basura_total.wav", 0.01f);
}

TestResult test_atari_wav() {
    return renderProgramWav("Switch to Atari and render 1.0s -> bin/atari.wav",
                            kBank3, kProg_Atari, "bin/atari.wav", 0.01f);
}

TestResult test_walking_filomena_wav() {
    return renderProgramWav("Switch to WalkingFilomena and render 1.0s -> bin/walking_filomena.wav",
                            kBank3, kProg_WalkingFilomena, "bin/walking_filomena.wav", 0.01f);
}

TestResult test_s_h_wav() {
    return renderProgramWav("Switch to S_H and render 1.0s -> bin/s_h.wav",
                            kBank3, kProg_S_H, "bin/s_h.wav", 0.01f);
}

TestResult test_array_on_the_rocks_wav() {
    return renderProgramWav("Switch to arrayOnTheRocks and render 1.0s -> bin/array_on_the_rocks.wav",
                            kBank3, kProg_arrayOnTheRocks, "bin/array_on_the_rocks.wav", 0.01f);
}

TestResult test_existencels_pain_wav() {
    return renderProgramWav("Switch to existencelsPain and render 1.0s -> bin/existencels_pain.wav",
                            kBank3, kProg_existencelsPain, "bin/existencels_pain.wav", 0.01f);
}

TestResult test_who_knows_wav() {
    return renderProgramWav("Switch to whoKnows and render 1.0s -> bin/who_knows.wav",
                            kBank3, kProg_whoKnows, "bin/who_knows.wav", 0.01f);
}

TestResult test_satan_workout_wav() {
    return renderProgramWav("Switch to satanWorkout and render 1.0s -> bin/satan_workout.wav",
                            kBank3, kProg_satanWorkout, "bin/satan_workout.wav", 0.01f);
}

TestResult test_rwalk_bitcrush_pw_wav() {
    return renderProgramWav("Switch to Rwalk_BitCrushPW and render 1.0s -> bin/rwalk_bitcrush_pw.wav",
                            kBank3, kProg_Rwalk_BitCrushPW, "bin/rwalk_bitcrush_pw.wav", 0.01f);
}

TestResult test_rwalk_lfree_wav() {
    return renderProgramWav("Switch to Rwalk_LFree and render 1.0s -> bin/rwalk_lfree.wav",
                            kBank3, kProg_Rwalk_LFree, "bin/rwalk_lfree.wav", 0.01f);
}

TestResult test_bank1_all_slots_audible() {
    TEST_BEGIN("All implemented Bank 1 slots produce audio after switch");
    struct SlotCheck {
        int program;
        const char* name;
        float minPeak;
    } checks[] = {
        { kProg_radioOhNo,          "radioOhNo",          0.01f },
        { kProg_Rwalk_SineFMFlange, "Rwalk_SineFMFlange", 0.01f },
        { kProg_xModRingSqr,        "xModRingSqr",        0.01f },
        { kProg_XModRingSine,       "XModRingSine",       0.01f },
        { kProg_CrossModRing,       "CrossModRing",       0.01f },
        { kProg_resonoise,          "resonoise",          0.01f },
        { kProg_grainGlitch,        "grainGlitch",        0.01f },
        { kProg_grainGlitchII,      "grainGlitchII",      0.01f },
        { kProg_grainGlitchIII,     "grainGlitchIII",     0.01f },
        { kProg_basurilla,          "basurilla",          0.01f },
    };

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin constructed");

    for (const auto& check : checks) {
        selectProgram(plugin, kBank1, check.program);
        float maxPeak = 0.0f;
        for (int i = 0; i < 16; ++i) {
            plugin.step(BLOCK_SIZE);
            const float* bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
            float p = PluginInstance::peak(bus, BLOCK_SIZE);
            if (p > maxPeak) maxPeak = p;
        }
        ASSERT_GT(maxPeak, check.minPeak, check.name);
    }

    TEST_PASS();
}

TestResult test_bank2_all_slots_audible() {
    TEST_BEGIN("All implemented Bank 2 slots produce audio after switch");
    struct SlotCheck {
        int program;
        const char* name;
        float minPeak;
    } checks[] = {
        { kProg_clusterSaw,       "clusterSaw",       0.01f },
        { kProg_pwCluster,        "pwCluster",        0.01f },
        { kProg_crCluster2,       "crCluster2",       0.01f },
        { kProg_sineFMcluster,    "sineFMcluster",    0.01f },
        { kProg_TriFMcluster,     "TriFMcluster",     0.01f },
        { kProg_PrimeCluster,     "PrimeCluster",     0.01f },
        { kProg_PrimeCnoise,      "PrimeCnoise",      0.01f },
        { kProg_FibonacciCluster, "FibonacciCluster", 0.01f },
        { kProg_partialCluster,   "partialCluster",   0.01f },
        { kProg_phasingCluster,   "phasingCluster",   0.01f },
    };

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin constructed");

    for (const auto& check : checks) {
        selectProgram(plugin, kBank2, check.program);
        float maxPeak = 0.0f;
        for (int i = 0; i < 16; ++i) {
            plugin.step(BLOCK_SIZE);
            const float* bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
            float p = PluginInstance::peak(bus, BLOCK_SIZE);
            if (p > maxPeak) maxPeak = p;
        }
        ASSERT_GT(maxPeak, check.minPeak, check.name);
    }

    TEST_PASS();
}

TestResult test_bank3_all_slots_audible() {
    TEST_BEGIN("All implemented Bank 3 slots produce audio after switch");
    struct SlotCheck {
        int program;
        const char* name;
        float minPeak;
    } checks[] = {
        { kProg_BasuraTotal,      "BasuraTotal",      0.01f },
        { kProg_Atari,            "Atari",            0.01f },
        { kProg_WalkingFilomena,  "WalkingFilomena",  0.01f },
        { kProg_S_H,              "S_H",              0.01f },
        { kProg_arrayOnTheRocks,  "arrayOnTheRocks",  0.01f },
        { kProg_existencelsPain,  "existencelsPain",  0.01f },
        { kProg_whoKnows,         "whoKnows",         0.01f },
        { kProg_satanWorkout,     "satanWorkout",     0.01f },
        { kProg_Rwalk_BitCrushPW, "Rwalk_BitCrushPW", 0.01f },
        { kProg_Rwalk_LFree,      "Rwalk_LFree",      0.01f },
    };

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin constructed");

    for (const auto& check : checks) {
        selectProgram(plugin, kBank3, check.program);
        float maxPeak = 0.0f;
        for (int i = 0; i < 16; ++i) {
            plugin.step(BLOCK_SIZE);
            const float* bus = plugin.getBus(OUTPUT_BUS, BLOCK_SIZE);
            float p = PluginInstance::peak(bus, BLOCK_SIZE);
            if (p > maxPeak) maxPeak = p;
        }
        ASSERT_GT(maxPeak, check.minPeak, check.name);
    }

    TEST_PASS();
}

// =============================================================================
// Regression: golden SHA-256 hashes for all WAV outputs
// (Verbatim port of PolyLofi/tests/test_integration.cpp::test_golden_wav_hashes)
// =============================================================================
TestResult test_golden_wav_hashes() {
    TEST_BEGIN("Golden WAV hashes (SHA-256 regression)");

    const char* goldenPath = "tests/golden_hashes.txt";

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

    int mismatches = 0;
    int regenerated = 0;
    for (auto& ln : lines) {
        if (!ln.isEntry) continue;

        ln.actual = sha256_file(ln.file.c_str());
        ASSERT_TRUE(!ln.actual.empty(),
                    (std::string("can read ") + ln.file).c_str());

        if (ln.hash == "*") {
            printf("    REGEN  %-35s %s\n", ln.file.c_str(), ln.actual.c_str());
            ++regenerated;
        } else if (ln.actual != ln.hash) {
            printf("    MISMATCH %-33s\n      expected: %s\n      actual:   %s\n",
                   ln.file.c_str(), ln.hash.c_str(), ln.actual.c_str());
            ++mismatches;
        }
    }

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

// =============================================================================
// Main
// =============================================================================
int main() {
    NtTestHarness::setSampleRate(48000);
    NtTestHarness::setMaxFrames(BLOCK_SIZE);

    return TestRunner::run({
        // *** WAV captures FIRST to pin noise seeds and stabilize golden hashes.
        // Adding any test later will not affect these seeds or hashes.
        // The order within this block IS sensitive — only append, never insert.
        test_white_noise_default_wav,
        test_white_noise_half_gain_wav,
        test_test_plugin_wav,
        test_teensy_alt_wav,
        test_cluster_saw_wav,
        test_pw_cluster_wav,
        test_cr_cluster2_wav,
        test_sine_fm_cluster_wav,
        test_tri_fm_cluster_wav,
        test_prime_cluster_wav,
        test_prime_cnoise_wav,
        test_fibonacci_cluster_wav,
        test_partial_cluster_wav,
        test_phasing_cluster_wav,
        test_basura_total_wav,
        test_atari_wav,
        test_walking_filomena_wav,
        test_s_h_wav,
        test_array_on_the_rocks_wav,
        test_existencels_pain_wav,
        test_who_knows_wav,
        test_satan_workout_wav,
        test_rwalk_bitcrush_pw_wav,
        test_rwalk_lfree_wav,

        // M3 effect-unit renders
        test_effect_bitcrusher_wav,
        test_effect_combine_wav,
        test_effect_multiply_wav,
        test_effect_wavefolder_wav,
        test_radio_oh_no_wav,
        test_rwalk_sine_fm_flange_wav,
        test_xmod_ring_sqr_wav,
        test_xmod_ring_sine_wav,
        test_cross_mod_ring_wav,
        test_resonoise_wav,
        test_grain_glitch_wav,
        test_grain_glitch_ii_wav,
        test_grain_glitch_iii_wav,
        test_basurilla_wav,

        // Regression check (comes after all WAV writers)
        test_golden_wav_hashes,

        // Lifecycle and functional tests (order doesn't affect golden hashes)
        test_plugin_loads,
        test_default_construct_audible,
        test_gain_zero_silent,
        test_replace_mode_overwrites_bus,
        test_add_mode_sums_with_bus,
        test_unrouted_no_crash,

        // Bank/Program switching (M2a)
        test_program_switch_to_test_plugin,
        test_program_switch_to_teensy_alt,
        test_xy_changes_are_smoothed,
        test_bank1_first_slot_audible,
        test_parameter_string_program_names,
        test_bank1_all_slots_audible,
        test_bank2_all_slots_audible,
        test_bank3_all_slots_audible,
    });
}
