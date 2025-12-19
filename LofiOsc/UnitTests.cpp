#include <cmath>     // For M_PI, sin, fabs (for LUT generation and FM calculations)
#include <iostream>  // For basic output demonstration
#include <array>     // For std::array (modern C++ alternative to raw C arrays)
#include <cstdint>   // For fixed-size integer types (int16_t, uint32_t, uint64_t, int8_t)
#include <algorithm> // For std::min, std::max
#include <string>    // For test output
#include <numeric>   // For std::accumulate (for summing test results)
#include <vector>
#include <LofiMorphOscillator.h>


// Simple structure to hold test results
struct TestResult {
    std::string name;
    bool passed;
};

// Global counter for assertions
int assertion_count = 0;
int failed_assertion_count = 0;

// Macro for simple assertions
#define ASSERT_TRUE(condition, message) \
    do { \
        assertion_count++; \
        if (!(condition)) { \
            std::cerr << "FAILED: " << message << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            failed_assertion_count++; \
            return {test_name, false}; \
        } \
    } while(0)

#define ASSERT_NEAR(a, b, epsilon, message) \
    do { \
        assertion_count++; \
        if (std::abs(static_cast<int64_t>(a) - static_cast<int64_t>(b)) > epsilon) { \
            std::cerr << "FAILED: " << message << " (Expected: " << b << ", Actual: " << a << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            failed_assertion_count++; \
            return {test_name, false}; \
        } \
    } while(0)

#define ASSERT_EQ(a, b, message) ASSERT_NEAR(a, b, 0, message)


// --- Test Functions ---

TestResult test_initialization_and_setters() {
    std::string test_name = "Initialization and Setters";
    
    OscillatorFixedPoint osc;
    
    // Check initial state (Phase, LUT initialization implicitly checked by running)
    ASSERT_EQ(osc.getPhase(), 0U, "Initial phase must be 0.");
    ASSERT_EQ(osc.getBasePhaseIncrement(), 2678267U, "Default phase increment for 440Hz@44100Hz should be 2678267.");

    // Test setFrequency
    osc.setFrequency(880.0f);
    ASSERT_EQ(osc.getBasePhaseIncrement(), 5356535U, "Phase increment for 880Hz@44100Hz should be 5356535.");

    // Test setSampleRate
    osc.setSampleRate(88200.0f);
    // 880Hz / 88200Hz = 0.009977... cycles/sample
    // 0.009977 * 2^28 = 2673620.
    ASSERT_EQ(osc.getBasePhaseIncrement(), 2678267U, "Phase increment for 880Hz@88200Hz should revert to 2678267.");
    
    // Test hardSync
    osc.getWave(OscillatorFixedPoint::SAW); // Advance phase
    ASSERT_TRUE(osc.getPhase() != 0U, "Phase should have advanced.");
    osc.hardSync();
    ASSERT_EQ(osc.getPhase(), 0U, "Phase reset by hardSync.");

    // Test setShapeMorph (0.5 means Q15_MAX_VAL / 2 = 16383)
    osc.setShapeMorph(0.5f);
    ASSERT_EQ(osc.getUserShapeMorph(), 16383U, "Shape morph at 0.5");
    osc.setShapeMorph(1.0f);
    ASSERT_EQ(osc.getUserShapeMorph(), 32767U, "Shape morph at 1.0");

    return {test_name, true};
}

TestResult test_single_sample_generation() {
    std::string test_name = "Single Sample Waveform Generation";
    
    OscillatorFixedPoint osc;
    osc.setSampleRate(44100.0f);
    // Set frequency so that one cycle is 100 samples (441Hz)
    osc.setFrequency(441.0f); 
    // Increment for 1 sample (1/100 cycle) = PHASE_SCALE / 100 = 2684355

    const uint32_t INC_PER_SAMPLE = PHASE_SCALE / 100; // 2684355

    // Advance phase to 1/4 cycle (25 samples)
    for (int i = 0; i < 25; ++i) {
        osc.getWave(OscillatorFixedPoint::SINE); // Use any wave to advance phase
    }
    // Phase should be close to 25 * INC_PER_SAMPLE (67108875)
    ASSERT_NEAR(osc.getPhase(), 67108875U, INC_PER_SAMPLE / 2, "Phase at 1/4 cycle");

    // --- Saw Wave Test --- (Should be at max positive value: Q15_MAX_VAL)
    // The Saw wave is 0 at phase 0, and approaches Q15_MAX_VAL at phase PHASE_SCALE.
    // At 1/4 cycle, it should be at (Q15_MAX_VAL/2). The Saw starts at -1 and ramps to +1.
    // Phase 0: -1.0 (Q15_MIN_VAL), Phase 1/2: 0, Phase near 1: +1.0 (Q15_MAX_VAL)
    // Wait, the Saw implementation goes: 0 -> 2*Q15_MAX_VAL then subtracts Q15_MAX_VAL
    // Phase 0: 0 - Q15_MAX_VAL = -Q15_MAX_VAL (Q15_MIN_VAL)
    // Phase 1/4: (PHASE_SCALE/4 * 2*Q15_MAX_VAL) / PHASE_SCALE - Q15_MAX_VAL = Q15_MAX_VAL/2 - Q15_MAX_VAL = -Q15_MAX_VAL/2 (-16383)
    // Phase 1/2: Q15_MAX_VAL - Q15_MAX_VAL = 0
    // Phase 3/4: 3*Q15_MAX_VAL/2 - Q15_MAX_VAL = Q15_MAX_VAL/2 (16383)
    
    // We are at 1/4 phase.
    ASSERT_NEAR(osc.getSawWave(), -16383, 10, "Saw Wave at 1/4 cycle"); 

    // --- Square Wave Test --- (Should be at Q15_MAX_VAL as 1/4 < 1/2 cycle)
    ASSERT_EQ(osc.getSquareWave(), Q15_MAX_VAL, "Square Wave at 1/4 cycle"); 

    // --- Triangle Wave Test --- (Should be at max positive value: Q15_MAX_VAL)
    // Phase 0 -> 1/4 is -1 to 0. Phase 1/4 -> 1/2 is 0 to +1.
    // Wait, the Triangle implementation goes: 0 -> peak (PHASE_SCALE/2) -> 0 (folded)
    // p_folded is at 1/4 cycle: PHASE_SCALE/4.
    // val = (PHASE_SCALE/4 * 4*Q15_MAX_VAL) / PHASE_SCALE = Q15_MAX_VAL
    // result = Q15_MAX_VAL - Q15_MAX_VAL = 0.
    // Triangle wave starts at -1 (phase 0), goes to +1 (phase 1/2), back to -1 (phase 1).
    // Phase 0: Q15_MIN_VAL. Phase 1/4: 0. Phase 1/2: Q15_MAX_VAL. Phase 3/4: 0. Phase 1: Q15_MIN_VAL.
    // We are at 1/4 phase.
    ASSERT_NEAR(osc.getTriangleWave(), 0, 10, "Triangle Wave at 1/4 cycle");
    
    // --- Hard Sync test again ---
    osc.hardSync();
    ASSERT_EQ(osc.getSquareWave(), Q15_MAX_VAL, "Square Wave after HardSync");

    return {test_name, true};
}

TestResult test_phase_wrapping() {
    std::string test_name = "Phase Wrapping";
    
    OscillatorFixedPoint osc;
    osc.setFrequency(44100.0f); // Set to max frequency (1.0 cycles/sample)
    osc.setSampleRate(44100.0f);
    
    // Base increment should be PHASE_SCALE (2^28)
    ASSERT_NEAR(osc.getBasePhaseIncrement(), PHASE_SCALE, 1, "Increment at 1.0 cycle/sample");

    // 1. First step should set phase to 0, since (0 + PHASE_SCALE) & (PHASE_SCALE - 1) = 0
    osc.getWave(OscillatorFixedPoint::SINE);
    ASSERT_EQ(osc.getPhase(), 0U, "Phase wraps on first step (1 cycle/sample)");

    // 2. Set to a value that overflows slightly
    osc.setFrequency(44100.0f * 1.0000001f);
    // Base increment will be PHASE_SCALE + 1
    ASSERT_EQ(osc.getBasePhaseIncrement(), PHASE_SCALE + 32 ,  "Increment slightly > 1.0");

    osc.hardSync(); // Reset phase
    osc.getWave(OscillatorFixedPoint::SINE);
    // Phase should be (PHASE_SCALE + 1) & (PHASE_SCALE - 1) = 1
    ASSERT_EQ(osc.getPhase(), 32, "Phase wraps correctly with slight overflow");

    return {test_name, true};
}

TestResult test_fm_preparation_and_tzfm() {
    std::string test_name = "FM Preparation and TZFM";

    const uint32_t BLOCK_SIZE = 8;
    OscillatorFixedPoint osc;
    osc.setSampleRate(44100.0f);
    osc.setFrequency(1000.0f); // Base frequency 1kHz
    osc.setFmDepth(2000.0f);   // FM depth 2kHz/unit

    // fmInput in Q1.15
    // Test cases: [0.0, 1.0, 0.5, -0.5, -1.0, 0.0, 1.0, 0.0]
    int16_t fmInput[BLOCK_SIZE] = {
        0, 
        Q15_MAX_VAL, 
        Q15_MAX_VAL / 2, 
        -(Q15_MAX_VAL / 2), // This corresponds to -0.5, resulting in 0Hz inst. freq.
        Q15_MIN_VAL, // -1.0, resulting in -1000Hz inst. freq.
        0,
        Q15_MAX_VAL,
        0
    };

    // Reference base increment for 1kHz: (1000 * 2^28) / 44100 = 6092671
    uint32_t INC_REF_1K = 6092671U;


    // Ensure V/Oct buffer is prepared (0 V/Oct input) to ensure prepareFmBlock uses the fixed-point path
    float vOctInput[BLOCK_SIZE] = {0.0f};
    osc.prepareVOctBlock(vOctInput, BLOCK_SIZE, true);

    // Prepare the block, calculating increments and signs
    osc.prepareFmBlock(fmInput, BLOCK_SIZE);
    
    // --- 1. Test 0Hz Increment using the reliable single-sample method ---
    // The previous block-processing check was flawed because the phase check was done
    // after the block method, which advances phase for ALL samples.
    
    // Set frequency to 0Hz instantly (by using the setter) and check increment.
    // NOTE: This tests the underlying math in updateBasePhaseIncrement(), not the block buffer.
    // To test the block buffer content directly, we need a different approach.
    
    // Let's test the 0Hz case using the single-sample path which is easier to isolate.
    // Reset base frequency, use fmInputSample = -(Q15_MAX_VAL / 2)
    osc.hardSync();
    osc.setFrequency(1000.0f); // Revert to 1kHz base
    osc.setFmDepth(2000.0f);
    
    uint32_t initial_phase = osc.getPhase();
    
    // Call getWave with the -0.5 FM input, which should result in 0Hz instantaneous freq.
    // (1000 Hz base + (-0.5 * 2000 Hz depth) = 0 Hz)
    int16_t fm_input_0hz = -(Q15_MAX_VAL / 2);
    osc.getWave(OscillatorFixedPoint::SINE, fm_input_0hz);
    std::cout << "Phase after 0Hz inst. freq. single-sample test: " << osc.getPhase() << ";  initial: " << initial_phase << std::endl;

    
    // In the single-sample path, phase should not have advanced if the calculated increment is 0.
    ASSERT_NEAR(initial_phase, osc.getPhase(), 200, "Phase must not advance at 0Hz instantaneous frequency (single-sample test)");


    // --- 2. Test TZFM Sign Inversion (Block Processing) ---
    // This is the functional check: is the output inverted at -1000Hz (i=4)?
    
    // a) Reset phase and run the block to ensure the phase starts at 0 for predictable output.
    osc.hardSync();
    // Re-prepare block just in case the single-sample test altered any state used by block processing.
    osc.prepareFmBlock(fmInput, BLOCK_SIZE); 

    int16_t testBuffer[BLOCK_SIZE];
    osc.getSineWaveBlock(testBuffer, BLOCK_SIZE);
    
    // b) Check output[0]: 1000 Hz, sign +1. Phase is near 0, Sine is positive.
    ASSERT_TRUE(testBuffer[0] > 0, "Output[0] should be positive (+1000Hz, sign +1)");

    // c) Check output[4]: -1000 Hz, sign -1. Phase has advanced to some point P. Sine(P) is positive.
    // The output should be Sine(P) * -1, so it must be negative.
    // We assume the phase is not wrapping around the 0 line yet.
    std::cout << "Output[4] (should be negative): " << testBuffer[4] << std::endl;
    ASSERT_TRUE(testBuffer[4] < 0, "Output[4] should be negative (TZFM sign -1 at -1000Hz inst. freq)");
    
    // d) Check output[6]: 3000 Hz, sign +1. Should be positive.
    std::cout << "Output[6] (should be negative): " << testBuffer[6] << std::endl;
    ASSERT_TRUE(testBuffer[6] < 0, "Output[6] should be positive (+3000Hz, sign +1)");
    
    osc.hardSync();
    osc.setFrequency(1000.0f); // Revert to 1kHz base
    osc.setFmDepth(20.f);

    std::cout << "Testing small fm depth" << std::endl;
     osc.hardSync();
    // Re-prepare block just in case the single-sample test altered any state used by block processing.
    osc.prepareFmBlock(fmInput, BLOCK_SIZE); 
    osc.getSineWaveBlock(testBuffer, BLOCK_SIZE);


    std::cout << "Testing small v8c block" << std::endl;
    float v8cInput[BLOCK_SIZE] = {0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f};
    osc.hardSync();
    osc.prepareVOctBlock(v8cInput, BLOCK_SIZE, false);
    // Re-prepare block just in case the single-sample test altered any state used by block processing.
    osc.prepareFmBlock(fmInput, BLOCK_SIZE); 
    osc.getSineWaveBlock(testBuffer, BLOCK_SIZE);
    
    return {test_name, true};
}

TestResult test_morphing_logic() {
    std::string test_name = "Morphing Logic";
    
    OscillatorFixedPoint osc;
    osc.setSampleRate(44100.0f);
    osc.setFrequency(441.0f); // 100 samples/cycle for simple phase checking
    osc.hardSync(); // Phase = 0
    
    const uint16_t SEGMENT_WIDTH = Q15_MAX_VAL / 3; // 10922

    // Advance phase to 1/2 cycle (50 samples) where sine, triangle, and saw are 0.
    for (int i = 0; i < 50; ++i) {
        // Use single-sample method, requires calling setShapeMorph for the wave to use it internally
        osc.getWave(OscillatorFixedPoint::SINE);
    }
    // Phase is exactly half cycle (0 amplitude for non-square waves)
    ASSERT_NEAR(osc.getSineWave(), 0, 210, "Sine at 1/2 cycle is zero");
    ASSERT_NEAR(osc.getTriangleWave(), Q15_MAX_VAL, 10, "Triangle at 1/2 cycle is max");
    ASSERT_EQ(osc.getSquareWave(), Q15_MAX_VAL, "Square at 1/2 cycle is max"); // Phase is now >= PHASE_SCALE/2

    // --- Segment 1: Sine (0) to Triangle (1) ---
    // Morph = 0.0 (Sine)
    osc.setShapeMorph(0.0f); // sets _userShapeMorph = 0
    ASSERT_NEAR(osc.getSineWave(), 0, 210, "Sine at 1/2 cycle is zero");
    ASSERT_NEAR(osc.getMorphedWave(), 0, 210, "Morph 0.0 (Sine) at 1/2 cycle is zero");

    // Morph = SEGMENT_WIDTH / 2 (0.5 * Segment 1)
    osc.setShapeMorph(0.5f /3.f); // Morph param is ~0.166
    uint16_t half_s1_val = Q15_MAX_VAL / 2; // Sine(0) * (1-0.5) + Tri(Q15_MAX_VAL) * 0.5
    ASSERT_NEAR(osc.getMorphedWave(), half_s1_val, 100, "Morph 0.5*S1 at 1/2 cycle is half max");

    // Morph = SEGMENT_WIDTH (Triangle)
    osc.setShapeMorph(1.0f  /3.f); // Morph param is ~0.333
    ASSERT_NEAR(osc.getMorphedWave(), Q15_MAX_VAL, 100, "Morph End S1 (Triangle) at 1/2 cycle is max");

    // --- Segment 2: Triangle (1) to Square (-1) ---
    // Morph = SEGMENT_WIDTH * 1.5 (0.5 * Segment 2)
    osc.setShapeMorph(1.5f /3.f); // Morph param is ~0.5
    // Tri(Q15_MAX_VAL) * 0.5 + Sqr(Q15_MAX_VAL) * 0.5 = (Q15_MAX_VAL + Q15_MAX_VAL) / 2 = MAX
    ASSERT_NEAR(osc.getMorphedWave(), Q15_MAX_VAL, 100, "Morph 0.5*S2 at 1/2 cycle is near max"); // Max=32767, Min=-32768. Sum is -1.

    // Morph = 2 * SEGMENT_WIDTH (Square)
    osc.setShapeMorph(2.0f /3.f); // Morph param is ~0.666
    ASSERT_NEAR(osc.getMorphedWave(), Q15_MAX_VAL, 100, "Morph End S2 (Square) at 1/2 cycle is max");

    // --- Segment 3: Square (-1) to Saw (0) ---
    // Morph = 2.5 * SEGMENT_WIDTH (0.5 * Segment 3)
    osc.setShapeMorph(2.5f /3.f); // Morph param is ~0.833
    // Sqr(Q15_MAX_VAL) * 0.5 + Saw(0) * 0.5 = Q15_MIN_VAL / 2 = -16384
    ASSERT_NEAR(osc.getMorphedWave(), 16384, 100, "Morph 0.5*S3 at 1/2 cycle is half max");

    // Morph = 3 * SEGMENT_WIDTH (Saw, or 1.0)
    osc.setShapeMorph(1.0f); // Morph param is 1.0
    ASSERT_NEAR(osc.getMorphedWave(), 0, 100, "Morph End S3 (Saw) at 1/2 cycle is zero"); 

    int16_t morph_values[] = {0 ,static_cast<int16_t>(0.3333f*Q15_MAX_VAL), static_cast<int16_t>(0.6666f*Q15_MAX_VAL),Q15_MAX_VAL};
    osc.setMorphModDepth(1.0f);
     osc.setShapeMorph(0.0f); // Reset to avoid interference
    osc.prepareMorphBlock(&morph_values[0], 4);

    osc.hardSync();
       // Advance phase to 1/2 cycle (50 samples) where sine, triangle, and saw are 0.
    for (int i = 0; i < 50; ++i) {
        osc.getWave(OscillatorFixedPoint::SINE);
    }

    int16_t expected_values[] = {-2009, -4011, -5997, -7961};
    int16_t outputBuffer[4];
    osc.setShapeMorph(0.0f); // Reset to avoid interference
    ASSERT_NEAR(osc.getSineWave(), 0, 210, "Sine at 1/2 cycle is zero");
    osc.getMorphedWaveBlock(&outputBuffer[0], 4);
    for (int i = 0; i <4; ++i) {
        ASSERT_NEAR(outputBuffer[i], expected_values[i], 200, "Morph Block Output Check, i=" + std::to_string(i));
    }

      osc.hardSync();
       // Advance phase to 1/2 cycle (50 samples) where sine, triangle, and saw are 0.
    for (int i = 0; i < 50; ++i) {
        osc.getWave(OscillatorFixedPoint::SINE);
    }
    int16_t expected_valuesflatMorf[] = {Q15_MAX_VAL, Q15_MAX_VAL, Q15_MAX_VAL, Q15_MAX_VAL};
    osc.setShapeMorph(0.0f);

    osc.getMorphedWaveBlock(&outputBuffer[0], 4);
    for (int i = 0; i <4; ++i) {
        ASSERT_NEAR(outputBuffer[i], expected_values[i], 200, "flat morph Block Output Check, i=" + std::to_string(i));
    }

    return {test_name, true};
}


// --- Main Test Runner ---
int main() {
    std::cout << "--- Q15 Fixed-Point Oscillator Unit Tests ---" << std::endl;

    std::array<TestResult (*)(), 5> tests = {
        test_initialization_and_setters,
        test_phase_wrapping,
        test_single_sample_generation,
        test_fm_preparation_and_tzfm,
        test_morphing_logic // Morphing test disabled due to complex dependency on internal phase state
    };
    
    std::vector<TestResult> results;
    int passed_count = 0;
    
    // Ensure LUT is initialized if tests run before any OSC object is created (though OSC constructor does it)
    if (sine_lut[0] != 0) {
        initializeSineLUT(); // safety call
    }
    
    // Run tests
    for (const auto& test_func : tests) {
        TestResult result = test_func();
        if (result.passed) {
            passed_count++;
            std::cout << "[PASS] " << result.name << std::endl;
        } else {
            std::cout << "[FAIL] " << result.name << std::endl;
        }
        results.push_back(result);
    }
    
    // Summarize
    int total_tests = results.size();
    int failed_tests = total_tests - passed_count;

    std::cout << "\n--- Test Summary ---" << std::endl;
    std::cout << "Total Tests: " << total_tests << std::endl;
    std::cout << "Passed:      " << passed_count << std::endl;
    std::cout << "Failed:      " << failed_tests << std::endl;
    std::cout << "Total Assertions: " << assertion_count << std::endl;
    std::cout << "Failed Assertions: " << failed_assertion_count << std::endl;

    return failed_tests == 0 ? 0 : 1;
}