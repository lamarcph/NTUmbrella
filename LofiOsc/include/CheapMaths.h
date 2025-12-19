#ifndef LOFI_PARTS_CHEAP_MATHS_H
#define LOFI_PARTS_CHEAP_MATHS_H

#include <cstdint>

static uint16_t xorshift_state = 12345; // Seed with a non-zero value

void seed_xorshift(uint16_t seed) {
    if (seed == 0) seed = 1; // Ensure seed is not zero
    xorshift_state = seed;
}

uint16_t xorshift16() {
    xorshift_state ^= (xorshift_state << 7);
    xorshift_state ^= (xorshift_state >> 9);
    xorshift_state ^= (xorshift_state << 8);
    return xorshift_state;
}

uint16_t get_triangular_dist(uint16_t max_val = UINT16_MAX) {
    uint32_t r1 = xorshift16();
    uint32_t r2 = xorshift16();

    // Summing two 16-bit numbers gives a range of 0 to 2*65535.
    // We need to scale this down.
    uint32_t sum = r1 + r2;

    // To scale to 0-MAX_VAL, we can use a division.
    // sum / (2*65535 / MAX_VAL) = sum * MAX_VAL / (2*65535)
    // Using a fixed-point multiply and shift:
    // (sum * K) >> SHIFT
    // K = MAX_VAL * 2^SHIFT / (2*65535)
    // For MAX_VAL = 4095, 2*65535 = 131070.
    // K = 4095 * 65536 / 131070 = 268308480 / 131070 = 2046.9
    // So K=2047, SHIFT=16
    const uint32_t K_TRI_SCALE = 2047; // Pre-calculated fixed point multiplier

    uint32_t result = (sum * K_TRI_SCALE) >> 16;

    // Clamp to max_val
    if (result > max_val) result = max_val;
    return static_cast<uint16_t>(result);
}

inline float uint16_to_float(uint16_t val, float x, float y) {
    return x + (y - x) * (static_cast<float>(val) / 65535.0f);
}

inline float clampf(float x, float lo, float hi) {
    return (x > hi) ? hi : ((x < lo) ? lo : x);
}


inline int16_t smoothStepQ15(int16_t start, int16_t end, int step, int numberOfSamples) {
    // Clamp step to [0, numberOfSamples]
    if (step <= 0) return start;
    if (step >= numberOfSamples) return end;
    // Linear interpolation in Q1.15
    int32_t diff = (int32_t)end - (int32_t)start;
    int32_t value = (int32_t)start + (diff * step) / numberOfSamples;
    return (int16_t)value;
}

// --- Configuration ---
// TABLE_BITS defines the size of the LUT. 
// 6 bits = 64 entries. This is small enough to stay in L1 Cache.
// Increasing this reduces error but increases cache pressure.
#define FM_LUT_BITS 6
#define FM_LUT_SIZE (1 << FM_LUT_BITS)
#define FM_LUT_MASK (FM_LUT_SIZE - 1)

// --- Constants ---
#define FM_LOG2_E    1.44269504088896340736f
#define FM_LOG2_10   3.32192809488736234787f
#define FM_LN_2      0.69314718055994530941f
#define FM_LOG10_2   0.30102999566398119521f

// --- Type Punning Union ---
// Allows bit-level access to floats without violating strict aliasing rules
typedef union {
    float f;
    int32_t i;
} fm_float_cast;



// --- Lookup Tables ---
// Generated for range [1.0, 2.0) for Log, and [0.0, 1.0) for Exp.
// Size is (1 << BITS) + 1 for easy interpolation.

// Table: log2(1.0 + x) where x is 0..1 in 64 steps
static const float _fm_log2_lut[FM_LUT_SIZE + 1] = {
    0.00000000f, 0.02236781f, 0.04439412f, 0.06608919f, 0.08746284f, 0.10852446f, 0.12928304f, 0.14974712f,
    0.16992500f, 0.18982456f, 0.20945336f, 0.22881869f, 0.24792751f, 0.26678680f, 0.28540221f, 0.30377899f,
    0.32192809f, 0.33985000f, 0.35755200f, 0.37503542f, 0.39230055f, 0.40934771f, 0.42617725f, 0.44278949f,
    0.45918484f, 0.47536376f, 0.49132670f, 0.50707419f, 0.52260662f, 0.53792448f, 0.55302828f, 0.56791857f,
    0.58259585f, 0.59706060f, 0.61131333f, 0.62535450f, 0.63918458f, 0.65280406f, 0.66621345f, 0.67941328f,
    0.69240409f, 0.70518641f, 0.71776077f, 0.73012766f, 0.74228753f, 0.75424083f, 0.76598801f, 0.77752949f,
    0.78886566f, 0.80000686f, 0.81095339f, 0.82170554f, 0.83226359f, 0.84262781f, 0.85280036f, 0.86278051f,
    0.87256860f, 0.88216503f, 0.89156921f, 0.90078061f, 0.90980070f, 0.91862998f, 0.92726900f, 0.93571833f,
    0.94397853f // +1 guard
};

// Table: 2^x where x is 0..1 in 64 steps
static const float _fm_exp2_lut[FM_LUT_SIZE + 1] = {
    1.00000000f, 1.01088929f, 1.02189715f, 1.03302488f, 1.04427378f, 1.05564516f, 1.06714035f, 1.07876066f,
    1.09050733f, 1.10238169f, 1.11438506f, 1.12651877f, 1.13878415f, 1.15118254f, 1.16371526f, 1.17638363f,
    1.18920712f, 1.20216024f, 1.21525353f, 1.22848760f, 1.24186307f, 1.25538056f, 1.26905070f, 1.28287411f,
    1.29685141f, 1.31098321f, 1.32526914f, 1.33970981f, 1.35430585f, 1.36905786f, 1.38396646f, 1.39903225f,
    1.41426587f, 1.42966800f, 1.44523928f, 1.46098041f, 1.47689212f, 1.49297512f, 1.50923015f, 1.52565791f,
    1.54226815f, 1.55906161f, 1.57603805f, 1.59319825f, 1.61054300f, 1.62807309f, 1.64578933f, 1.66369252f,
    1.68178347f, 1.70006299f, 1.71853189f, 1.73719099f, 1.75604111f, 1.77508309f, 1.79431777f, 1.81374600f,
    1.83336853f, 1.85318621f, 1.87320089f, 1.89341443f, 1.91382869f, 1.93444552f, 1.95526680f, 1.97629438f,
    2.00000000f // +1 guard
};

// --- Core Implementation --- From Gemini

/**
 * Fast Log2 approximation.
 * Strategy: x = M * 2^E. log2(x) = E + log2(M).
 * We extract E directly from float bits. We look up log2(M) in the table.
 */
static inline float fast_log2f(float x) {
    if (x <= 0.0f) return -1e30f; // Minimal error handling for speed

    fm_float_cast u = { .f = x };
    
    // Extract Exponent (IEEE 754: bits 23-30)
    int32_t exponent = ((u.i >> 23) & 0xFF) - 127;
    
    // Extract Mantissa (bits 0-22) and treat as 1.Mantissa
    // We mask out the exponent and put in a '0' exponent (bias 127) to get range [1, 2)
    u.i &= 0x007FFFFF;
    u.i |= 0x3F800000;
    
    // Calculate Table Index
    // We use the top bits of the mantissa for the index.
    // Mantissa is 23 bits. We want FM_LUT_BITS (6). 
    // Shift: 23 - 6 = 17.
    const int32_t mantissa_bits = u.i & 0x007FFFFF;
    const int32_t index = mantissa_bits >> (23 - FM_LUT_BITS);
    
    // Calculate fractional part for interpolation
    // We take the remaining bits that we shifted out
    const float frac = (float)(mantissa_bits & 0x0001FFFF) * (1.0f / 131072.0f); // 131072 = 2^17
    
    // Linear Interpolation
    float y0 = _fm_log2_lut[index];
    float y1 = _fm_log2_lut[index + 1];
    
    return (float)exponent + y0 + frac * (y1 - y0);
}

/**
 * Fast Exp2 approximation.
 * Strategy: x = I + F. 2^x = 2^I * 2^F.
 * 2^I is done by integer addition to exponent field.
 * 2^F is looked up.
 */
static inline float fast_exp2f(float x) {
    // Clamp to prevent overflow/underflow if necessary, but omitting for pure speed
    // if (x < -126.0f) return 0.0f;
    // if (x > 128.0f) return HUGE_VALF;

    float int_part_f = floorf(x);
    int32_t int_part = (int32_t)int_part_f;
    float frac_part = x - int_part_f;

    // LUT Index
    // frac_part is 0.0 to 1.0. Scale to 0..64
    float index_f = frac_part * (float)FM_LUT_SIZE;
    int32_t index = (int32_t)index_f;
    float lerp_frac = index_f - (float)index;

    // Linear Interpolation for 2^F
    float y0 = _fm_exp2_lut[index];
    float y1 = _fm_exp2_lut[index + 1];
    float res_frac = y0 + lerp_frac * (y1 - y0);

    // Reconstruct float: result * 2^int_part
    fm_float_cast u = { .f = res_frac };
    u.i += (int_part << 23);

    return u.f;
}

// --- Derived Functions ---

static inline float fast_powf(float base, float exp) {
    // x^y = 2^(y * log2(x))
    return fast_exp2f(exp * fast_log2f(base));
}

static inline float fast_expf(float x) {
    // e^x = 2^(x * log2(e))
    return fast_exp2f(x * FM_LOG2_E);
}

static inline float fast_exp10f(float x) {
    // 10^x = 2^(x * log2(10))
    return fast_exp2f(x * FM_LOG2_10);
}

static inline float fast_logf(float x) {
    // ln(x) = log2(x) * ln(2)
    return fast_log2f(x) * FM_LN_2;
}

static inline float fast_log10f(float x) {
    // log10(x) = log2(x) * log10(2)
    return fast_log2f(x) * FM_LOG10_2;
}

#endif // LOFI_PARTS_CHEAP_MATHS_H