#include "test_framework.h"
#include "plugin_harness.h"
#include "wav_writer.h"
#include "sha256.h"

#include <vector>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>

static constexpr int BLOCK = 64;
static constexpr int IN_L_BUS  = 0;
static constexpr int IN_R_BUS  = 1;
static constexpr int CV_FILT_BUS = 2;
static constexpr int CV_RING_BUS = 3;
static constexpr int OUT_L_BUS = 12;
static constexpr int OUT_R_BUS = 13;

enum {
    kParamInL = 0, kParamInR,
    kParamOutL, kParamOutLMode,
    kParamOutR, kParamOutRMode,

    kParamCrossLo, kParamCrossHi,
    kParamDrive,
    kParamPressLo, kParamPressMid, kParamPressHi,
    kParamMix, kParamOutput,

    kParamFilterModel, kParamFilterMode,
    kParamFilterCutoff, kParamFilterRes, kParamFilterDrive,
    kParamFilterOversample,

    kParamBitDepthLo, kParamBitDepthMid, kParamBitDepthHi,
    kParamDecimationLo, kParamDecimationMid, kParamDecimationHi,
    kParamNoiseLevelLo, kParamNoiseLevelMid, kParamNoiseLevelHi,
    kParamNoiseColor,

    kParamRingFreq,
    kParamRingDepthLo, kParamRingDepthMid, kParamRingDepthHi,
    kParamWidthLo, kParamWidthMid, kParamWidthHi,

    kParamEnvSensitivity,
    kParamEnvAttack, kParamEnvRelease, kParamEnvShape,
    kParamTransientAttackLo,  kParamTransientAttackMid,  kParamTransientAttackHi,
    kParamTransientSustainLo, kParamTransientSustainMid, kParamTransientSustainHi,

    kParamGritLo, kParamGritMid, kParamGritHi,

    kParamFilterCutoffEnv, kParamFilterDriveEnv, kParamFilterResEnv,

    kParamCVFilterFreqIn, kParamCVFilterFreqDepth,
    kParamCVRingFreqIn,   kParamCVRingFreqDepth,

    kParamInputHP,
    kParamBias,

    kParamOutputLP,
    kParamOutLimiter,
    kParamLoSatMode,
};

struct LoadedWav {
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    std::vector<float> left;
    std::vector<float> right;
};

struct RenderStats {
    float peakL = 0.0f;
    float peakR = 0.0f;
    float meanAbsDiffL = 0.0f;
    float meanAbsDiffR = 0.0f;
};

typedef void (*BlockHook)(PluginInstance& plugin, int blockIndex, int totalBlocks);

static uint16_t readLe16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t readLe32(const uint8_t* p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static bool loadWav(const char* path, LoadedWav& wav) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    uint8_t header[12];
    if (fread(header, 1, sizeof(header), f) != sizeof(header)) {
        fclose(f);
        return false;
    }
    if (std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0) {
        fclose(f);
        return false;
    }

    uint16_t fmtAudioFormat = 0;
    uint16_t fmtChannels = 0;
    uint32_t fmtSampleRate = 0;
    uint16_t fmtBitsPerSample = 0;
    std::vector<uint8_t> pcmData;

    while (true) {
        uint8_t chunkHeader[8];
        if (fread(chunkHeader, 1, sizeof(chunkHeader), f) != sizeof(chunkHeader)) break;

        const uint32_t chunkSize = readLe32(chunkHeader + 4);
        const bool isFmt = (std::memcmp(chunkHeader, "fmt ", 4) == 0);
        const bool isData = (std::memcmp(chunkHeader, "data", 4) == 0);

        if (isFmt) {
            std::vector<uint8_t> fmt(chunkSize);
            if (fread(fmt.data(), 1, chunkSize, f) != chunkSize) {
                fclose(f);
                return false;
            }
            if (chunkSize < 16) {
                fclose(f);
                return false;
            }
            fmtAudioFormat = readLe16(&fmt[0]);
            fmtChannels = readLe16(&fmt[2]);
            fmtSampleRate = readLe32(&fmt[4]);
            fmtBitsPerSample = readLe16(&fmt[14]);
        } else if (isData) {
            pcmData.resize(chunkSize);
            if (chunkSize > 0 && fread(pcmData.data(), 1, chunkSize, f) != chunkSize) {
                fclose(f);
                return false;
            }
        } else {
            if (fseek(f, (long)chunkSize, SEEK_CUR) != 0) {
                fclose(f);
                return false;
            }
        }

        if (chunkSize & 1) {
            if (fseek(f, 1, SEEK_CUR) != 0) {
                fclose(f);
                return false;
            }
        }
    }

    fclose(f);

    const bool isPcm = (fmtAudioFormat == 1);
    const bool isFloat = (fmtAudioFormat == 3);
    const bool supportedBits = (fmtBitsPerSample == 16 || fmtBitsPerSample == 24 || fmtBitsPerSample == 32);
    if ((!isPcm && !isFloat) || (fmtChannels != 1 && fmtChannels != 2) || !supportedBits || pcmData.empty()) {
        return false;
    }

    const size_t bytesPerSample = (size_t)fmtBitsPerSample / 8u;
    const size_t frameBytes = bytesPerSample * (size_t)fmtChannels;
    const size_t numFrames = pcmData.size() / frameBytes;
    if (numFrames == 0) return false;

    wav.sampleRate = fmtSampleRate;
    wav.channels = fmtChannels;
    wav.left.resize(numFrames);
    wav.right.resize(numFrames);

    const uint8_t* s = pcmData.data();
    for (size_t i = 0; i < numFrames; ++i) {
        auto readSample = [&](size_t frameIndex, int ch) -> float {
            const size_t base = frameIndex * frameBytes + (size_t)ch * bytesPerSample;
            if (isFloat && fmtBitsPerSample == 32) {
                float v = 0.0f;
                std::memcpy(&v, s + base, sizeof(float));
                return v;
            }

            int32_t raw = 0;
            if (fmtBitsPerSample == 16) {
                raw = (int16_t)readLe16(s + base);
                return (float)raw / 32768.0f;
            }
            if (fmtBitsPerSample == 24) {
                raw = (int32_t)(s[base] | (s[base + 1] << 8) | (s[base + 2] << 16));
                if (raw & 0x00800000) raw |= ~0x00FFFFFF;
                return (float)raw / 8388608.0f;
            }
            raw = (int32_t)(s[base] | (s[base + 1] << 8) | (s[base + 2] << 16) | (s[base + 3] << 24));
            return (float)raw / 2147483648.0f;
        };

        const float l = readSample(i, 0);
        const float r = (fmtChannels == 2) ? readSample(i, 1) : l;
        wav.left[i] = l;
        wav.right[i] = r;
    }

    return true;
}

static bool createPlugin(PluginInstance& p, uint32_t sampleRate = 96000) {
    NtTestHarness::setSampleRate(sampleRate);
    NtTestHarness::setMaxFrames(BLOCK);
    if (!p.load(0)) return false;
    p.initStatic();
    return p.construct();
}

static void setBaseRouting(PluginInstance& plugin) {
    plugin.setParameter(kParamOutLMode, 1);
    plugin.setParameter(kParamOutRMode, 1);
    plugin.setParameter(kParamMix, 1000);
    plugin.setParameter(kParamOutput, 1000);
}

static RenderStats processLoopToWav(
    PluginInstance& plugin,
    const LoadedWav& wav,
    const char* outPath,
    int loopRepeats,
    BlockHook hook = nullptr
) {
    RenderStats stats;

    WavWriter out(outPath, NtTestHarness::getSampleRate(), 2);
    if (!out.isOpen()) return stats;

    std::vector<float> inBlkL(BLOCK, 0.0f);
    std::vector<float> inBlkR(BLOCK, 0.0f);

    uint64_t samplesCompared = 0;
    double sumAbsDiffL = 0.0;
    double sumAbsDiffR = 0.0;

    size_t framePos = 0;
    const size_t totalFrames = wav.left.size() * (size_t)loopRepeats;
    const int totalBlocks = (int)((totalFrames + (size_t)BLOCK - 1u) / (size_t)BLOCK);
    int blockIndex = 0;

    while (framePos < totalFrames) {
        const int n = (int)std::min<size_t>(BLOCK, totalFrames - framePos);

        for (int i = 0; i < n; ++i) {
            const size_t src = (framePos + (size_t)i) % wav.left.size();
            inBlkL[i] = wav.left[src];
            inBlkR[i] = wav.right[src];
        }
        for (int i = n; i < BLOCK; ++i) {
            inBlkL[i] = 0.0f;
            inBlkR[i] = 0.0f;
        }

        if (hook) hook(plugin, blockIndex, totalBlocks);

        plugin.prepareStep(BLOCK);
        plugin.fillBus(IN_L_BUS, inBlkL.data(), BLOCK);
        plugin.fillBus(IN_R_BUS, inBlkR.data(), BLOCK);
        plugin.executeStep(BLOCK);

        const float* outL = plugin.getBus(OUT_L_BUS, BLOCK);
        const float* outR = plugin.getBus(OUT_R_BUS, BLOCK);

        out.writeStereo(outL, outR, n);

        const float peakL = PluginInstance::peak(outL, n);
        const float peakR = PluginInstance::peak(outR, n);
        if (peakL > stats.peakL) stats.peakL = peakL;
        if (peakR > stats.peakR) stats.peakR = peakR;

        for (int i = 0; i < n; ++i) {
            sumAbsDiffL += std::fabs(outL[i] - inBlkL[i]);
            sumAbsDiffR += std::fabs(outR[i] - inBlkR[i]);
        }
        samplesCompared += (uint64_t)n;
        framePos += (size_t)n;
        ++blockIndex;
    }

    if (samplesCompared > 0) {
        stats.meanAbsDiffL = (float)(sumAbsDiffL / (double)samplesCompared);
        stats.meanAbsDiffR = (float)(sumAbsDiffR / (double)samplesCompared);
    }

    return stats;
}

TestResult test_plugin_loads() {
    TEST_BEGIN("Nerberus plugin loads");
    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin), "plugin constructed");
    ASSERT_NOT_NULL(plugin.factory(), "factory available");
    ASSERT_TRUE(plugin.numParameters() >= 62, "expected expanded parameter count");
    TEST_PASS();
}

TestResult test_loop1_wav_loads() {
    TEST_BEGIN("loop1.wav loads as supported WAV mono/stereo");
    LoadedWav wav;
    ASSERT_TRUE(loadWav("testWav/loop1.wav", wav), "loaded testWav/loop1.wav");
    ASSERT_TRUE(wav.left.size() > 0, "wav has frames");
    ASSERT_TRUE(wav.channels == 1 || wav.channels == 2, "wav channels supported");
    ASSERT_TRUE(wav.sampleRate > 0, "wav sample rate valid");
    TEST_PASS();
}

TestResult test_showcase_dry_passthrough_wav() {
    TEST_BEGIN("Showcase: dry passthrough (writes bin/Nerberus_loop_dry.wav)");
    LoadedWav wav;
    ASSERT_TRUE(loadWav("testWav/loop1.wav", wav), "loaded loop1.wav");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin, wav.sampleRate), "plugin constructed");
    setBaseRouting(plugin);
    plugin.setParameter(kParamMix, 0);

    RenderStats stats = processLoopToWav(plugin, wav, "bin/Nerberus_loop_dry.wav", 2);
    ASSERT_GT(stats.peakL, 0.01f, "dry left output has signal");
    ASSERT_GT(stats.peakR, 0.01f, "dry right output has signal");
    ASSERT_LT(stats.meanAbsDiffL, 1e-5f, "dry left remains unchanged");
    ASSERT_LT(stats.meanAbsDiffR, 1e-5f, "dry right remains unchanged");
    TEST_PASS();
}

TestResult test_showcase_full_stack_wav() {
    TEST_BEGIN("Showcase: full texture stack (writes bin/Nerberus_loop_full_stack.wav)");
    LoadedWav wav;
    ASSERT_TRUE(loadWav("testWav/loop1.wav", wav), "loaded loop1.wav");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin, wav.sampleRate), "plugin constructed");
    setBaseRouting(plugin);

    plugin.setParameter(kParamDrive, 700);
    plugin.setParameter(kParamPressLo,  300);
    plugin.setParameter(kParamPressMid, 200);
    plugin.setParameter(kParamPressHi,  550);
    plugin.setParameter(kParamGritLo, 450);
    plugin.setParameter(kParamGritMid, 450);
    plugin.setParameter(kParamGritHi, 450);

    plugin.setParameter(kParamFilterModel, 2);   // MS20
    plugin.setParameter(kParamFilterMode, 1);    // LP4
    plugin.setParameter(kParamFilterCutoff, 7250);  // ~3 kHz (exponential 0-10000 scale)
    plugin.setParameter(kParamFilterRes, 650);
    plugin.setParameter(kParamFilterDrive, 1500);  // 1.5x drive

    plugin.setParameter(kParamBitDepthMid, 6);      // crush mids
    plugin.setParameter(kParamDecimationMid, 3);    // decimate mids

    plugin.setParameter(kParamNoiseLevelHi, 50);    // subtle air/hiss on highs
    plugin.setParameter(kParamNoiseColor, 1);       // Pink

    plugin.setParameter(kParamRingFreq, 3500);
    plugin.setParameter(kParamRingDepthMid, 300);   // ring mod mids

    plugin.setParameter(kParamTransientAttackMid,  420);
    plugin.setParameter(kParamTransientSustainMid, -250);
    plugin.setParameter(kParamEnvSensitivity, 600);

    plugin.setParameter(kParamWidthLo,  600);       // narrow lows (club-safe)
    plugin.setParameter(kParamWidthMid, 1200);
    plugin.setParameter(kParamWidthHi,  1800);      // wide highs
    plugin.setParameter(kParamMix, 850);
    plugin.setParameter(kParamOutput, 900);

    RenderStats stats = processLoopToWav(plugin, wav, "bin/Nerberus_loop_full_stack.wav", 2);
    ASSERT_GT(stats.peakL, 0.01f, "full stack left has signal");
    ASSERT_GT(stats.peakR, 0.01f, "full stack right has signal");
    ASSERT_GT(stats.meanAbsDiffL, 0.001f, "full stack changes left channel");
    ASSERT_GT(stats.meanAbsDiffR, 0.001f, "full stack changes right channel");
    TEST_PASS();
}

static void filterSweepHook(PluginInstance& plugin, int blockIndex, int totalBlocks) {
    // Sweep LP cutoff from ~400 Hz to ~8 kHz using the exponential 0-10000 scale
    // raw=4337 ≈ 400 Hz,  raw=8674 ≈ 8 kHz  (freq = 20 * 1000^(raw/10000))
    int cutoff = 4337 + (blockIndex * (8674 - 4337)) / std::max(1, totalBlocks - 1);
    plugin.setParameter(kParamFilterCutoff, (int16_t)cutoff);
}

TestResult test_showcase_filter_sweep_wav() {
    TEST_BEGIN("Showcase: filter sweep (writes bin/Nerberus_loop_filter_sweep.wav)");
    LoadedWav wav;
    ASSERT_TRUE(loadWav("testWav/loop1.wav", wav), "loaded loop1.wav");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin, wav.sampleRate), "plugin constructed");
    setBaseRouting(plugin);

    plugin.setParameter(kParamDrive, 400);
    plugin.setParameter(kParamGritLo, 200);
    plugin.setParameter(kParamGritMid, 200);
    plugin.setParameter(kParamGritHi, 200);
    // SVF is more stable than Ladder at moderate resonance values
    plugin.setParameter(kParamFilterModel, 0);  // SVF
    plugin.setParameter(kParamFilterMode, 1);   // LP4
    // Resonance 0.4 — audible peak without self-oscillation risk
    plugin.setParameter(kParamFilterRes, 400);
    // FilterDrive 1.0x — neutral
    plugin.setParameter(kParamFilterDrive, 1000);
    plugin.setParameter(kParamMix, 1000);

    RenderStats stats = processLoopToWav(plugin, wav, "bin/Nerberus_loop_filter_sweep.wav", 2, filterSweepHook);
    ASSERT_GT(stats.peakL, 0.01f, "filter sweep left has signal");
    ASSERT_GT(stats.peakR, 0.01f, "filter sweep right has signal");
    ASSERT_GT(stats.meanAbsDiffL, 0.001f, "filter sweep alters left channel");
    ASSERT_GT(stats.meanAbsDiffR, 0.001f, "filter sweep alters right channel");
    TEST_PASS();
}

TestResult test_showcase_spunk_hihat_wav() {
    TEST_BEGIN("Showcase: spunk hi-hat chain (writes bin/Nerberus_loop_spunk_hihat.wav)");
    LoadedWav wav;
    ASSERT_TRUE(loadWav("testWav/loop1.wav", wav), "loaded loop1.wav");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin, wav.sampleRate), "plugin constructed");
    setBaseRouting(plugin);

    // HP at ~2800 Hz (raw 7154 on exponential scale)
    plugin.setParameter(kParamFilterModel, 0);  // SVF
    plugin.setParameter(kParamFilterMode, 2);   // HP2
    plugin.setParameter(kParamFilterCutoff, 7154);  // ~2800 Hz
    plugin.setParameter(kParamFilterRes, 280);  // gentle peak, not aggressive

    // Subtle 8-bit crush on mids + light decimation on highs
    plugin.setParameter(kParamBitDepthMid, 8);
    plugin.setParameter(kParamDecimationHi, 2);

    // Noise: air/hiss only in hi band
    plugin.setParameter(kParamNoiseLevelHi, 25);  // very subtle hi-band texture
    plugin.setParameter(kParamNoiseColor, 0);   // White

    // Ring mod: subtle shimmer at 3kHz, mid band only
    plugin.setParameter(kParamRingFreq, 3000);
    plugin.setParameter(kParamRingDepthMid, 150);

    // Transient shaper: add snap to mids, tighten hi tail
    plugin.setParameter(kParamTransientAttackMid,  350);
    plugin.setParameter(kParamTransientSustainHi, -250);

    // Width: narrow lows (mono kick), normal mid, wide hi
    plugin.setParameter(kParamWidthLo,  500);
    plugin.setParameter(kParamWidthMid, 1000);
    plugin.setParameter(kParamWidthHi,  1500);
    plugin.setParameter(kParamDrive, 450);
    plugin.setParameter(kParamGritLo, 350);
    plugin.setParameter(kParamGritMid, 350);
    plugin.setParameter(kParamGritHi, 350);
    plugin.setParameter(kParamEnvSensitivity, 400);
    plugin.setParameter(kParamMix, 800);
    plugin.setParameter(kParamOutput, 1000);

    RenderStats stats = processLoopToWav(plugin, wav, "bin/Nerberus_loop_spunk_hihat.wav", 2);
    ASSERT_GT(stats.peakL, 0.01f, "spunk chain left has signal");
    ASSERT_GT(stats.peakR, 0.01f, "spunk chain right has signal");
    ASSERT_GT(stats.meanAbsDiffL, 0.001f, "spunk chain alters left channel");
    ASSERT_GT(stats.meanAbsDiffR, 0.001f, "spunk chain alters right channel");
    TEST_PASS();
}

TestResult test_transient_shaper_wav() {
    TEST_BEGIN("Transient shaper: attack boost and tail tighten (writes bin/Nerberus_loop_transient.wav)");
    LoadedWav wav;
    ASSERT_TRUE(loadWav("testWav/loop1.wav", wav), "loaded loop1.wav");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin, wav.sampleRate), "plugin constructed");
    setBaseRouting(plugin);

    // Engine: light drive only so transient shaper effect is clearly audible
    plugin.setParameter(kParamDrive, 200);
    plugin.setParameter(kParamGritLo, 0);
    plugin.setParameter(kParamGritMid, 0);
    plugin.setParameter(kParamGritHi, 0);
    plugin.setParameter(kParamPressLo,  0);
    plugin.setParameter(kParamPressMid, 0);
    plugin.setParameter(kParamPressHi,  0);

    // Transient shaper: strong attack boost, significant tail tighten
    // Attack +700: loud initial click/snap boost when transient envelope peaks
    // Sustain -500: reduces gain during sustained body portion
    plugin.setParameter(kParamTransientAttackMid,  700);
    plugin.setParameter(kParamTransientSustainMid, -500);

    // No filter, no noise, no ring mod — isolate transient shaper effect
    plugin.setParameter(kParamFilterMode, 6);   // Bypass
    plugin.setParameter(kParamNoiseLevelLo, 0);
    plugin.setParameter(kParamNoiseLevelMid, 0);
    plugin.setParameter(kParamNoiseLevelHi, 0);
    plugin.setParameter(kParamRingDepthLo, 0);
    plugin.setParameter(kParamRingDepthMid, 0);
    plugin.setParameter(kParamRingDepthHi, 0);
    plugin.setParameter(kParamWidthLo,  1000);      // unity width
    plugin.setParameter(kParamWidthMid, 1000);
    plugin.setParameter(kParamWidthHi,  1000);
    plugin.setParameter(kParamEnvSensitivity, 0);

    plugin.setParameter(kParamMix, 1000);
    plugin.setParameter(kParamOutput, 1000);

    RenderStats stats = processLoopToWav(plugin, wav, "bin/Nerberus_loop_transient.wav", 2);
    ASSERT_GT(stats.peakL, 0.01f, "transient shaped left has signal");
    ASSERT_GT(stats.peakR, 0.01f, "transient shaped right has signal");
    // With attack boost, peaks should be higher than dry — signal is meaningfully different
    ASSERT_GT(stats.meanAbsDiffL, 0.001f, "transient shaper changes left channel");
    ASSERT_GT(stats.meanAbsDiffR, 0.001f, "transient shaper changes right channel");
    TEST_PASS();
}

TestResult test_ring_modulator_wav() {
    TEST_BEGIN("Ring modulator: metallic shimmer at three frequencies (writes bin/Nerberus_loop_ringmod.wav)");
    LoadedWav wav;
    ASSERT_TRUE(loadWav("testWav/loop1.wav", wav), "loaded loop1.wav");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin, wav.sampleRate), "plugin constructed");
    setBaseRouting(plugin);

    // Light engine processing so ring mod sidebands are clearly audible
    plugin.setParameter(kParamDrive, 300);
    plugin.setParameter(kParamGritLo, 0);
    plugin.setParameter(kParamGritMid, 0);
    plugin.setParameter(kParamGritHi, 0);

    // Ring mod at 50% depth — full wet/dry blend
    // 1200 Hz: bell-like inharmonic shimmer on transients
    plugin.setParameter(kParamRingFreq, 1200);
    plugin.setParameter(kParamRingDepthMid, 500);  // ring mod the mids

    // No filter, no noise, no transient shaper — isolate ring modulator
    plugin.setParameter(kParamFilterMode, 6);   // Bypass
    plugin.setParameter(kParamNoiseLevelLo, 0);
    plugin.setParameter(kParamNoiseLevelMid, 0);
    plugin.setParameter(kParamNoiseLevelHi, 0);
    plugin.setParameter(kParamTransientAttackLo, 0);
    plugin.setParameter(kParamTransientAttackMid, 0);
    plugin.setParameter(kParamTransientAttackHi, 0);
    plugin.setParameter(kParamTransientSustainLo, 0);
    plugin.setParameter(kParamTransientSustainMid, 0);
    plugin.setParameter(kParamTransientSustainHi, 0);
    plugin.setParameter(kParamWidthLo,  1000);
    plugin.setParameter(kParamWidthMid, 1000);
    plugin.setParameter(kParamWidthHi,  1000);
    plugin.setParameter(kParamEnvSensitivity, 0);

    plugin.setParameter(kParamMix, 1000);
    plugin.setParameter(kParamOutput, 1000);

    RenderStats stats = processLoopToWav(plugin, wav, "bin/Nerberus_loop_ringmod.wav", 2);
    ASSERT_GT(stats.peakL, 0.01f, "ring mod left has signal");
    ASSERT_GT(stats.peakR, 0.01f, "ring mod right has signal");
    // At 50% ring depth the output is meaningfully different from dry
    ASSERT_GT(stats.meanAbsDiffL, 0.01f, "ring mod changes left channel");
    ASSERT_GT(stats.meanAbsDiffR, 0.01f, "ring mod changes right channel");
    TEST_PASS();
}

TestResult test_cv_filter_freq_modulation() {
    TEST_BEGIN("CV filter freq: rising CV sweeps filter open (writes bin/Nerberus_cv_filter_freq.wav)");
    LoadedWav wav;
    ASSERT_TRUE(loadWav("testWav/loop1.wav", wav), "loaded loop1.wav");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin, wav.sampleRate), "plugin constructed");
    setBaseRouting(plugin);

    // LP filter at a low base cutoff so CV sweep is clearly audible
    plugin.setParameter(kParamFilterModel, 0);   // SVF
    plugin.setParameter(kParamFilterMode, 1);    // LP4
    plugin.setParameter(kParamFilterCutoff, 4337); // ~400 Hz base
    plugin.setParameter(kParamFilterRes, 300);

    // Route CV filter freq to CV_FILT_BUS (1-based: CV_FILT_BUS+1)
    plugin.setParameter(kParamCVFilterFreqIn,    (int16_t)(CV_FILT_BUS + 1));
    plugin.setParameter(kParamCVFilterFreqDepth, 2000);  // 2.0 oct/V

    WavWriter out("bin/Nerberus_cv_filter_freq.wav", NtTestHarness::getSampleRate(), 2);
    ASSERT_TRUE(out.isOpen(), "output wav opened");

    std::vector<float> inBlkL(BLOCK, 0.0f);
    std::vector<float> inBlkR(BLOCK, 0.0f);
    std::vector<float> cvBlk(BLOCK, 0.0f);

    const size_t totalFrames = wav.left.size() * 2;
    const int totalBlocks = (int)((totalFrames + BLOCK - 1) / BLOCK);
    size_t framePos = 0;
    int blockIndex = 0;
    float peakOut = 0.f;

    while (framePos < totalFrames) {
        const int n = (int)std::min<size_t>(BLOCK, totalFrames - framePos);
        for (int i = 0; i < n; ++i) {
            const size_t src = (framePos + (size_t)i) % wav.left.size();
            inBlkL[i] = wav.left[src];
            inBlkR[i] = wav.right[src];
        }
        for (int i = n; i < BLOCK; ++i) inBlkL[i] = inBlkR[i] = 0.0f;

        // CV ramps from 0V to +4V  → sweeps cutoff from ~400 Hz up by ~8 octaves
        const float cvVal = 4.0f * (float)blockIndex / (float)std::max(1, totalBlocks - 1);
        std::fill(cvBlk.begin(), cvBlk.end(), cvVal);

        plugin.prepareStep(BLOCK);
        plugin.fillBus(IN_L_BUS,    inBlkL.data(), BLOCK);
        plugin.fillBus(IN_R_BUS,    inBlkR.data(), BLOCK);
        plugin.fillBus(CV_FILT_BUS, cvBlk.data(),  BLOCK);
        plugin.executeStep(BLOCK);

        const float* outL = plugin.getBus(OUT_L_BUS, BLOCK);
        const float* outR = plugin.getBus(OUT_R_BUS, BLOCK);
        out.writeStereo(outL, outR, n);

        const float pk = PluginInstance::peak(outL, n);
        if (pk > peakOut) peakOut = pk;

        framePos += (size_t)n;
        ++blockIndex;
    }

    ASSERT_GT(peakOut, 0.01f, "CV filter freq modulation produces audible output");
    TEST_PASS();
}

TestResult test_cv_ring_freq_modulation() {
    TEST_BEGIN("CV ring freq: sine CV wobbles ring frequency (writes bin/Nerberus_cv_ring_freq.wav)");
    LoadedWav wav;
    ASSERT_TRUE(loadWav("testWav/loop1.wav", wav), "loaded loop1.wav");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin, wav.sampleRate), "plugin constructed");
    setBaseRouting(plugin);

    // Ring mod at 100% depth on mids so pitch wobble is clearly audible
    plugin.setParameter(kParamRingFreq,     1000);  // 1 kHz base carrier
    plugin.setParameter(kParamRingDepthMid,  800);  // 80% depth
    plugin.setParameter(kParamFilterMode, 6);       // Bypass filter

    // Route CV ring freq to CV_RING_BUS (1-based: CV_RING_BUS+1)
    plugin.setParameter(kParamCVRingFreqIn,    (int16_t)(CV_RING_BUS + 1));
    plugin.setParameter(kParamCVRingFreqDepth, 2000);  // 2.0 oct/V

    WavWriter out("bin/Nerberus_cv_ring_freq.wav", NtTestHarness::getSampleRate(), 2);
    ASSERT_TRUE(out.isOpen(), "output wav opened");

    std::vector<float> inBlkL(BLOCK, 0.0f);
    std::vector<float> inBlkR(BLOCK, 0.0f);
    std::vector<float> cvBlk(BLOCK, 0.0f);

    const size_t totalFrames = wav.left.size() * 2;
    const int totalBlocks = (int)((totalFrames + BLOCK - 1) / BLOCK);
    size_t framePos = 0;
    int blockIndex = 0;
    float peakOut = 0.f;

    while (framePos < totalFrames) {
        const int n = (int)std::min<size_t>(BLOCK, totalFrames - framePos);
        for (int i = 0; i < n; ++i) {
            const size_t src = (framePos + (size_t)i) % wav.left.size();
            inBlkL[i] = wav.left[src];
            inBlkR[i] = wav.right[src];
        }
        for (int i = n; i < BLOCK; ++i) inBlkL[i] = inBlkR[i] = 0.0f;

        // CV: slow sine (one full cycle across the render) wobbles ring freq ±2 oct
        const float phase = 2.0f * 3.14159265f * (float)blockIndex / (float)std::max(1, totalBlocks - 1);
        const float cvVal = std::sin(phase);  // ±1 V
        std::fill(cvBlk.begin(), cvBlk.end(), cvVal);

        plugin.prepareStep(BLOCK);
        plugin.fillBus(IN_L_BUS,    inBlkL.data(), BLOCK);
        plugin.fillBus(IN_R_BUS,    inBlkR.data(), BLOCK);
        plugin.fillBus(CV_RING_BUS, cvBlk.data(),  BLOCK);
        plugin.executeStep(BLOCK);

        const float* outL = plugin.getBus(OUT_L_BUS, BLOCK);
        const float* outR = plugin.getBus(OUT_R_BUS, BLOCK);
        out.writeStereo(outL, outR, n);

        const float pk = PluginInstance::peak(outL, n);
        if (pk > peakOut) peakOut = pk;

        framePos += (size_t)n;
        ++blockIndex;
    }

    ASSERT_GT(peakOut, 0.01f, "CV ring freq modulation produces audible output");
    TEST_PASS();
}

TestResult test_env_dest_drive_sensitivity_wav() {
    TEST_BEGIN("Env destination: Drive via Env Sens (writes bin/Nerberus_env_dest_drive.wav)");
    LoadedWav wav;
    ASSERT_TRUE(loadWav("testWav/loop1.wav", wav), "loaded loop1.wav");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin, wav.sampleRate), "plugin constructed");
    setBaseRouting(plugin);

    // Isolate env->drive destination: bypass filter so only engine drive movement is heard.
    plugin.setParameter(kParamFilterMode, 6); // Bypass
    plugin.setParameter(kParamDrive, 600);
    plugin.setParameter(kParamEnvSensitivity, -1000);
    plugin.setParameter(kParamEnvAttack, 0);
    plugin.setParameter(kParamEnvRelease, 350);
    plugin.setParameter(kParamEnvShape, -300);

    plugin.setParameter(kParamGritLo, 450);
    plugin.setParameter(kParamGritMid, 450);
    plugin.setParameter(kParamGritHi, 450);
    plugin.setParameter(kParamMix, 1000);
    plugin.setParameter(kParamOutput, 1000);

    RenderStats stats = processLoopToWav(plugin, wav, "bin/Nerberus_env_dest_drive.wav", 2);
    ASSERT_GT(stats.peakL, 0.01f, "env->drive left has signal");
    ASSERT_GT(stats.peakR, 0.01f, "env->drive right has signal");
    ASSERT_GT(stats.meanAbsDiffL, 0.001f, "env->drive changes left channel");
    ASSERT_GT(stats.meanAbsDiffR, 0.001f, "env->drive changes right channel");
    TEST_PASS();
}

TestResult test_env_dest_filter_cutoff_wav() {
    TEST_BEGIN("Env destination: Filter Cutoff Env (writes bin/Nerberus_env_dest_filter_cutoff.wav)");
    LoadedWav wav;
    ASSERT_TRUE(loadWav("testWav/loop1.wav", wav), "loaded loop1.wav");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin, wav.sampleRate), "plugin constructed");
    setBaseRouting(plugin);

    // Isolate cutoff modulation with neutral drive/res env amounts.
    plugin.setParameter(kParamFilterModel, 0);   // SVF
    plugin.setParameter(kParamFilterMode, 1);    // LP4
    plugin.setParameter(kParamFilterCutoff, 4500); // ~450 Hz base
    plugin.setParameter(kParamFilterRes, 250);
    plugin.setParameter(kParamFilterDrive, 1000);

    plugin.setParameter(kParamFilterCutoffEnv, 1000);
    plugin.setParameter(kParamFilterDriveEnv, 0);
    plugin.setParameter(kParamFilterResEnv, 0);

    plugin.setParameter(kParamEnvSensitivity, 0);
    plugin.setParameter(kParamEnvAttack, 0);
    plugin.setParameter(kParamEnvRelease, 150);
    plugin.setParameter(kParamEnvShape, -300);

    plugin.setParameter(kParamMix, 1000);
    plugin.setParameter(kParamOutput, 1000);

    RenderStats stats = processLoopToWav(plugin, wav, "bin/Nerberus_env_dest_filter_cutoff.wav", 2);
    ASSERT_GT(stats.peakL, 0.01f, "env->cutoff left has signal");
    ASSERT_GT(stats.peakR, 0.01f, "env->cutoff right has signal");
    ASSERT_GT(stats.meanAbsDiffL, 0.001f, "env->cutoff changes left channel");
    ASSERT_GT(stats.meanAbsDiffR, 0.001f, "env->cutoff changes right channel");
    TEST_PASS();
}

TestResult test_env_dest_filter_drive_wav() {
    TEST_BEGIN("Env destination: Filter Drive Env (writes bin/Nerberus_env_dest_filter_drive.wav)");
    LoadedWav wav;
    ASSERT_TRUE(loadWav("testWav/loop1.wav", wav), "loaded loop1.wav");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin, wav.sampleRate), "plugin constructed");
    setBaseRouting(plugin);

    // Isolate filter drive modulation with fixed cutoff/resonance.
    plugin.setParameter(kParamFilterModel, 2);   // MS20
    plugin.setParameter(kParamFilterMode, 1);    // LP4
    plugin.setParameter(kParamFilterCutoff, 7000);
    plugin.setParameter(kParamFilterRes, 220);
    plugin.setParameter(kParamFilterDrive, 1000);

    plugin.setParameter(kParamFilterCutoffEnv, 0);
    plugin.setParameter(kParamFilterDriveEnv, 1000);
    plugin.setParameter(kParamFilterResEnv, 0);

    plugin.setParameter(kParamEnvSensitivity, 0);
    plugin.setParameter(kParamEnvAttack, 0);
    plugin.setParameter(kParamEnvRelease, 180);
    plugin.setParameter(kParamEnvShape, -300);

    plugin.setParameter(kParamMix, 1000);
    plugin.setParameter(kParamOutput, 1000);

    RenderStats stats = processLoopToWav(plugin, wav, "bin/Nerberus_env_dest_filter_drive.wav", 2);
    ASSERT_GT(stats.peakL, 0.01f, "env->filter drive left has signal");
    ASSERT_GT(stats.peakR, 0.01f, "env->filter drive right has signal");
    ASSERT_GT(stats.meanAbsDiffL, 0.001f, "env->filter drive changes left channel");
    ASSERT_GT(stats.meanAbsDiffR, 0.001f, "env->filter drive changes right channel");
    TEST_PASS();
}

TestResult test_env_dest_filter_res_wav() {
    TEST_BEGIN("Env destination: Filter Res Env (writes bin/Nerberus_env_dest_filter_res.wav)");
    LoadedWav wav;
    ASSERT_TRUE(loadWav("testWav/loop1.wav", wav), "loaded loop1.wav");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin, wav.sampleRate), "plugin constructed");
    setBaseRouting(plugin);

    // Isolate resonance modulation with fixed cutoff/drive.
    plugin.setParameter(kParamFilterModel, 0);   // SVF
    plugin.setParameter(kParamFilterMode, 1);    // LP4
    plugin.setParameter(kParamFilterCutoff, 6500);
    plugin.setParameter(kParamFilterRes, 820);
    plugin.setParameter(kParamFilterDrive, 1000);

    plugin.setParameter(kParamFilterCutoffEnv, 1000);
    plugin.setParameter(kParamFilterDriveEnv, 0);
    plugin.setParameter(kParamFilterResEnv, -1000);

    plugin.setParameter(kParamEnvSensitivity, 0);
    plugin.setParameter(kParamEnvAttack, 75);
    plugin.setParameter(kParamEnvRelease, 275);
    plugin.setParameter(kParamEnvShape, -300);

    plugin.setParameter(kParamMix, 1000);
    plugin.setParameter(kParamOutput, 1000);

    RenderStats stats = processLoopToWav(plugin, wav, "bin/Nerberus_env_dest_filter_res.wav", 2);
    ASSERT_GT(stats.peakL, 0.01f, "env->filter res left has signal");
    ASSERT_GT(stats.peakR, 0.01f, "env->filter res right has signal");
    ASSERT_GT(stats.meanAbsDiffL, 0.001f, "env->filter res changes left channel");
    ASSERT_GT(stats.meanAbsDiffR, 0.001f, "env->filter res changes right channel");
    TEST_PASS();
}

// -------------------------------------------------------------------------
// Output LP, soft limiter, Lo asymmetric saturation
// -------------------------------------------------------------------------

TestResult test_output_lp_cab_rolloff_wav() {
    TEST_BEGIN("Output LP: cab rolloff at 6 kHz (writes bin/Nerberus_output_lp.wav)");
    LoadedWav wav;
    ASSERT_TRUE(loadWav("testWav/loop1.wav", wav), "loaded loop1.wav");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin, wav.sampleRate), "plugin constructed");
    setBaseRouting(plugin);

    // Heavy drive + grit to push energy into high frequencies first
    plugin.setParameter(kParamDrive, 600);
    plugin.setParameter(kParamGritLo,  400);
    plugin.setParameter(kParamGritMid, 400);
    plugin.setParameter(kParamGritHi,  400);

    // Output LP at 6 kHz: -3dB at 6 kHz, significant rolloff above 8 kHz.
    // Simulates a close-miked guitar cabinet where the speaker + mic
    // combination naturally rolls off the fizzy hi-frequency artefacts
    // that saturation adds.
    plugin.setParameter(kParamOutputLP, 6000);

    // Bypass post filter so only the output LP is doing the HF shaping
    plugin.setParameter(kParamFilterMode, 6);  // Bypass
    plugin.setParameter(kParamMix, 1000);
    plugin.setParameter(kParamOutput, 1000);

    RenderStats stats = processLoopToWav(plugin, wav, "bin/Nerberus_output_lp.wav", 2);
    ASSERT_GT(stats.peakL, 0.01f, "output LP left has signal");
    ASSERT_GT(stats.peakR, 0.01f, "output LP right has signal");
    // LP at 6 kHz on a heavily driven signal removes a significant amount of
    // high-frequency harmonic content — output must differ substantially from dry
    ASSERT_GT(stats.meanAbsDiffL, 0.001f, "output LP changes left channel");
    ASSERT_GT(stats.meanAbsDiffR, 0.001f, "output LP changes right channel");
    TEST_PASS();
}

TestResult test_output_soft_limiter_wav() {
    TEST_BEGIN("Output soft limiter: saturating ceiling on hot signal (writes bin/Nerberus_output_limiter.wav)");
    LoadedWav wav;
    ASSERT_TRUE(loadWav("testWav/loop1.wav", wav), "loaded loop1.wav");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin, wav.sampleRate), "plugin constructed");
    setBaseRouting(plugin);

    // Very hot signal: max drive + high grit pushes peaks well above unity.
    // Without the limiter this signal would clip at the next stage.
    plugin.setParameter(kParamDrive, 1000);
    plugin.setParameter(kParamGritLo,  800);
    plugin.setParameter(kParamGritMid, 800);
    plugin.setParameter(kParamGritHi,  800);

    // Full output soft limiter: at this amount cheap_saturate is blended in at
    // 100%, providing a smooth knee that catches peaks above ~±1.0.
    plugin.setParameter(kParamOutLimiter, 1000);

    plugin.setParameter(kParamFilterMode, 6);  // Bypass
    plugin.setParameter(kParamMix, 1000);
    plugin.setParameter(kParamOutput, 1000);

    RenderStats stats = processLoopToWav(plugin, wav, "bin/Nerberus_output_limiter.wav", 2);
    ASSERT_GT(stats.peakL, 0.01f, "soft-limited left has signal");
    ASSERT_GT(stats.peakR, 0.01f, "soft-limited right has signal");
    ASSERT_GT(stats.meanAbsDiffL, 0.001f, "limiter changes left channel vs dry");
    ASSERT_GT(stats.meanAbsDiffR, 0.001f, "limiter changes right channel vs dry");
    // cheap_saturate hard-clips at ±3 — no output should exceed that
    ASSERT_LT(stats.peakL, 3.1f, "peak stays within cheap_saturate hard clip");
    ASSERT_LT(stats.peakR, 3.1f, "peak stays within cheap_saturate hard clip");
    TEST_PASS();
}

TestResult test_lo_asym_saturation_wav() {
    TEST_BEGIN("Lo Asym: asymmetric low-band saturation (writes bin/Nerberus_lo_asym.wav)");
    LoadedWav wav;
    ASSERT_TRUE(loadWav("testWav/loop1.wav", wav), "loaded loop1.wav");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin, wav.sampleRate), "plugin constructed");
    setBaseRouting(plugin);

    // Asymmetric Lo mode: x + 0.10x² generates even-order harmonics in the low band.
    // On a drum break the kick and snare fundamentals acquire tube-like 2nd harmonic
    // content — warmer, more forward, characteristic of an overdriven transformer or
    // power tube operating slightly beyond its Class-A bias point.
    plugin.setParameter(kParamLoSatMode, 1);

    // Drive the Lo band to make the asymmetry clearly audible
    plugin.setParameter(kParamDrive, 700);
    plugin.setParameter(kParamPressLo, 200);  // light Lo compression to keep levels even
    plugin.setParameter(kParamGritLo, 300);   // wave fold after asymmetric saturation
    plugin.setParameter(kParamGritMid, 0);
    plugin.setParameter(kParamGritHi,  0);

    // Tight crossover to isolate the Lo band effect
    plugin.setParameter(kParamCrossLo, 200);
    plugin.setParameter(kParamCrossHi, 3000);

    plugin.setParameter(kParamFilterMode, 6);  // Bypass
    plugin.setParameter(kParamMix, 1000);
    plugin.setParameter(kParamOutput, 1000);

    RenderStats stats = processLoopToWav(plugin, wav, "bin/Nerberus_lo_asym.wav", 2);
    ASSERT_GT(stats.peakL, 0.01f, "Lo asym left has signal");
    ASSERT_GT(stats.peakR, 0.01f, "Lo asym right has signal");
    ASSERT_GT(stats.meanAbsDiffL, 0.001f, "Lo asym changes left channel");
    ASSERT_GT(stats.meanAbsDiffR, 0.001f, "Lo asym changes right channel");
    TEST_PASS();
}

// -------------------------------------------------------------------------
// Input HP conditioning
// -------------------------------------------------------------------------

TestResult test_input_hp_conditioning_wav() {
    TEST_BEGIN("Input HP: sub removal before saturation (writes bin/Nerberus_input_hp.wav)");
    LoadedWav wav;
    ASSERT_TRUE(loadWav("testWav/loop1.wav", wav), "loaded loop1.wav");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin, wav.sampleRate), "plugin constructed");
    setBaseRouting(plugin);

    // HP at 300 Hz: strips kick fundamental (~60-80 Hz) and snare body (~150-250 Hz)
    // before the crossover + saturation chain.  At this frequency on a 70s drum break
    // the low-end content is clearly audible — its removal is easy to verify.
    plugin.setParameter(kParamInputHP, 300);

    // Moderate drive + grit on all bands so the saturation difference is audible
    plugin.setParameter(kParamDrive, 500);
    plugin.setParameter(kParamGritLo,  400);
    plugin.setParameter(kParamGritMid, 400);
    plugin.setParameter(kParamGritHi,  400);

    // Bypass post filter — isolate the HP conditioning effect
    plugin.setParameter(kParamFilterMode, 6);  // Bypass
    plugin.setParameter(kParamMix, 1000);
    plugin.setParameter(kParamOutput, 1000);

    RenderStats stats = processLoopToWav(plugin, wav, "bin/Nerberus_input_hp.wav", 2);
    ASSERT_GT(stats.peakL, 0.01f, "HP-conditioned left has signal");
    ASSERT_GT(stats.peakR, 0.01f, "HP-conditioned right has signal");
    // At 300 Hz the removed sub-bass is significant — output must differ from input
    ASSERT_GT(stats.meanAbsDiffL, 0.001f, "HP conditioning changes left channel");
    ASSERT_GT(stats.meanAbsDiffR, 0.001f, "HP conditioning changes right channel");
    TEST_PASS();
}

// -------------------------------------------------------------------------
// Dynamic bias
// -------------------------------------------------------------------------

TestResult test_dynamic_bias_wav() {
    TEST_BEGIN("Dynamic bias: transient-driven even-order harmonics (writes bin/Nerberus_dynamic_bias.wav)");
    LoadedWav wav;
    ASSERT_TRUE(loadWav("testWav/loop1.wav", wav), "loaded loop1.wav");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin, wav.sampleRate), "plugin constructed");
    setBaseRouting(plugin);

    // Maximum bias: envDrive * 1.0 * 0.08 -> up to +8% DC offset on lo/mid
    // during hard transients (kick/snare attacks on this break).
    plugin.setParameter(kParamBias, 1000);

    // Envelope follower: fast attack to latch onto drum transients instantly,
    // 300 ms decay so bias lingers through the body then fades on the tail.
    plugin.setParameter(kParamEnvSensitivity, 800);
    plugin.setParameter(kParamEnvAttack,   0);    // instant snap
    plugin.setParameter(kParamEnvRelease, 300);
    plugin.setParameter(kParamEnvShape,  -500);   // sharp log decay

    // Moderate drive + lo/mid grit so the asymmetric clipping from the bias
    // offset is clearly audible as additional even-order harmonic content.
    plugin.setParameter(kParamDrive, 500);
    plugin.setParameter(kParamGritLo,  500);
    plugin.setParameter(kParamGritMid, 500);
    plugin.setParameter(kParamGritHi,    0);  // hi band untouched for reference

    // Bypass post filter — isolate the bias + saturation effect
    plugin.setParameter(kParamFilterMode, 6);  // Bypass
    plugin.setParameter(kParamMix, 1000);
    plugin.setParameter(kParamOutput, 1000);

    RenderStats stats = processLoopToWav(plugin, wav, "bin/Nerberus_dynamic_bias.wav", 2);
    ASSERT_GT(stats.peakL, 0.01f, "dynamic bias left has signal");
    ASSERT_GT(stats.peakR, 0.01f, "dynamic bias right has signal");
    // Bias + grit saturation generates significant even-order content — output
    // differs considerably from the linear dry signal
    ASSERT_GT(stats.meanAbsDiffL, 0.001f, "dynamic bias changes left channel");
    ASSERT_GT(stats.meanAbsDiffR, 0.001f, "dynamic bias changes right channel");
    TEST_PASS();
}

// -------------------------------------------------------------------------
// Golden hash verification
// -------------------------------------------------------------------------

TestResult test_classic_multiband_wav() {
    TEST_BEGIN("Classic multiband: mono lows / glued mids / wide highs (writes bin/Nerberus_loop_classic.wav)");
    LoadedWav wav;
    ASSERT_TRUE(loadWav("testWav/loop1.wav", wav), "loaded loop1.wav");

    PluginInstance plugin;
    ASSERT_TRUE(createPlugin(plugin, wav.sampleRate), "plugin constructed");
    setBaseRouting(plugin);

    // Tighter crossover: tight sub below 180Hz, airy highs above 2800Hz
    plugin.setParameter(kParamCrossLo, 180);
    plugin.setParameter(kParamCrossHi, 2800);

    // Light overall drive — warmth only
    plugin.setParameter(kParamDrive, 250);

    // Per-band dynamics (Press):
    // Lo +400: compress the kick body, prevents pumping, adds glue
    // Mid -200: downward expansion, snare and transients pop more
    // Hi +150: tame harsh cymbal peaks
    plugin.setParameter(kParamPressLo,   400);
    plugin.setParameter(kParamPressMid, -200);
    plugin.setParameter(kParamPressHi,   150);

    // Per-band width:
    // Lo: nearly mono (350) — mono kick/sub for club/broadcast compatibility
    // Mid: unity (1000) — snare image untouched
    // Hi: wide (1700) — spacious cymbals and air
    plugin.setParameter(kParamWidthLo,   350);
    plugin.setParameter(kParamWidthMid, 1000);
    plugin.setParameter(kParamWidthHi,  1700);

    // Hi band: very subtle pink noise for air
    plugin.setParameter(kParamNoiseLevelHi, 20);
    plugin.setParameter(kParamNoiseColor, 1);  // Pink

    // No filter, no crush, no ring mod, no transient shaper
    plugin.setParameter(kParamFilterMode, 6);  // Bypass

    plugin.setParameter(kParamMix, 1000);
    plugin.setParameter(kParamOutput, 1000);

    RenderStats stats = processLoopToWav(plugin, wav, "bin/Nerberus_loop_classic.wav", 2);
    ASSERT_GT(stats.peakL, 0.01f, "classic left has signal");
    ASSERT_GT(stats.peakR, 0.01f, "classic right has signal");
    ASSERT_GT(stats.meanAbsDiffL, 0.001f, "classic processing changes left channel");
    ASSERT_GT(stats.meanAbsDiffR, 0.001f, "classic processing changes right channel");
    TEST_PASS();
}

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
        if (sep == std::string::npos) {
            lines.push_back(ln);
            continue;
        }

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

int main() {
    return TestRunner::run({
        test_plugin_loads,
        test_loop1_wav_loads,
        test_showcase_dry_passthrough_wav,
        test_showcase_full_stack_wav,
        test_transient_shaper_wav,
        test_ring_modulator_wav,
        test_showcase_filter_sweep_wav,
        test_showcase_spunk_hihat_wav,
        test_classic_multiband_wav,
        test_cv_filter_freq_modulation,
        test_cv_ring_freq_modulation,
        test_env_dest_drive_sensitivity_wav,
        test_env_dest_filter_cutoff_wav,
        test_env_dest_filter_drive_wav,
        test_env_dest_filter_res_wav,
        test_input_hp_conditioning_wav,
        test_dynamic_bias_wav,
        test_output_lp_cab_rolloff_wav,
        test_output_soft_limiter_wav,
        test_lo_asym_saturation_wav,
        test_golden_wav_hashes,
    });
}
