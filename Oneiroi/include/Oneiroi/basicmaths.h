#ifndef __basicmaths_h__
#define __basicmaths_h__

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define _USE_MATH_DEFINES


#define M_E        2.71828182845904523536
#define M_LOG2E    1.44269504088896340736
#define M_LOG10E   0.434294481903251827651
#define M_LN2      0.693147180559945309417
#define M_LN10     2.30258509299404568402
#define M_PI       3.14159265358979323846
#define M_PI_2     1.57079632679489661923
#define M_PI_4     0.785398163397448309616
#define M_1_PI     0.318309886183790671538
#define M_2_PI     0.636619772367581343076
#define M_2_SQRTPI 1.12837916709551257390
#define M_SQRT2    1.41421356237309504880
#define M_SQRT1_2  0.707106781186547524401

#ifdef ARM_CORTEX
//#include "arm_math.h" 
#endif //ARM_CORTEX

#ifdef __cplusplus
#define _USE_MATH_DEFINES
#include <cmath>
#include <math.h>
#include <algorithm>
using std::min;
using std::max;
using std::abs;
#define clamp(x, lo, hi) ((x)>(hi)?(hi):((x)<(lo)?(lo):(x)))
#else
#include <math.h>
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif
#ifndef abs
#define abs(x) ((x)>0?(x):-(x))
#endif
#ifndef clamp
#define clamp(x, lo, hi) ((x)>(hi)?(hi):((x)<(lo)?(lo):(x)))
#endif
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_SQRT2
#define M_SQRT2 1.41421356237309504880
#endif


#ifdef __cplusplus
 extern "C" {
#endif

   float arm_sqrtf(float in);
   void arm_srand32(uint32_t s);
   uint32_t arm_rand32();

   // fast lookup-based exponentials
   

   /** generate a random number between 0 and 1 */
   float randf();

   float fast_fmodf(float x, float y);


#ifdef ARM_CORTEX
/*#define malloc(x) pvPortMalloc(x)
#define calloc(x, y) pvPortCalloc(x, y)
#define free(x) vPortFree(x)
#define realloc(x, y) pvPortRealloc(x, y);
void* pvPortCalloc(size_t nmemb, size_t size);
void* pvPortRealloc(void *pv, size_t xWantedSize);*/
#endif

#ifdef __cplusplus
}
#endif

#ifdef ARM_CORTEX
#define sin(x) arm_sin_f32(x)
#define sinf(x) arm_sin_f32(x)
#define cos(x) arm_cos_f32(x)
#define cosf(x) arm_cos_f32(x)
#define sqrt(x) sqrtf(x)
/* #define sqrtf(x) arm_sqrtf(x) */
#define rand() arm_rand32()

#undef RAND_MAX
#define RAND_MAX UINT32_MAX
#endif //ARM_CORTEX


#ifdef ARM_CORTEX
/* The realloc() function changes the size of the memory block pointed  to */
/* by ptr to size bytes.  The contents will be unchanged in the range from */
/* the start of the region up to the minimum of the old and new sizes.  If */
/* the  new size is larger than the old size, the added memory will not be */
/* initialized.  If ptr is NULL, then  the  call  is  equivalent  to  mal‐ */
/* loc(size), for all values of size; if size is equal to zero, and ptr is */
/* not NULL, then the call is equivalent  to  free(ptr).   Unless  ptr  is */
/* NULL,  it  must have been returned by an earlier call to malloc(), cal‐ */
/* loc(), or realloc().  If the area pointed to was moved, a free(ptr)  is */
/* done. */
/*void* pvPortRealloc(void *ptr, size_t new_size) {
  if(ptr == NULL) 
    return pvPortMalloc(new_size);
  size_t old_size = vPortGetSizeBlock(ptr);
  if(new_size == 0){
    vPortFree(ptr);
    return NULL;
  }
  if(new_size <= old_size)
    return ptr;
  void* p = pvPortMalloc(new_size);
  if(p == NULL)
    return p;
  memcpy(p, ptr, old_size);
  vPortFree(ptr);
  return p;
} */

/* The calloc() function allocates memory for an array of  nmemb  elements */
/* of  size bytes each and returns a pointer to the allocated memory. */
/* The memory is set to zero. */
void *pvPortCalloc(size_t nmemb, size_t size){						  
  size_t xWantedSize = nmemb*size;
  void* ptr = pvPortMalloc(xWantedSize);
  if(ptr != NULL)
    memset(ptr, 0, xWantedSize);
  return ptr;
}
#endif

// todo: see
// http://www.hxa.name/articles/content/fast-pow-adjustable_hxa7241_2007.html
// http://www.finesse.demon.co.uk/steven/sqrt.html
// http://www.keil.com/forum/7934/
// http://processors.wiki.ti.com/index.php/ARM_compiler_optimizations

  /* void *_sbrk(intptr_t increment){} */

static uint32_t r32seed = 33641;

void arm_srand32(uint32_t s){
  r32seed = s;
}

/**
 * Generate an unsigned 32bit pseudo-random number using xorshifter algorithm. Aka xorshifter32.
 * "Anyone who considers arithmetical methods of producing random digits is, of course, in a state of sin." 
 * -- John von Neumann.
*/
uint32_t arm_rand32(){
  r32seed ^= r32seed << 13;
  r32seed ^= r32seed >> 17;
  r32seed ^= r32seed << 5;
  return r32seed;
}

float randf(){
  return arm_rand32()*(1/4294967296.0f);
}

float arm_sqrtf(float in){
  float out;
#ifdef ARM_CORTEX
  arm_sqrt_f32(in, &out);
#else
  out=sqrtf(in);
#endif
  return out;
}

/* Fast arctan2
 * from http://dspguru.com/dsp/tricks/fixed-point-atan2-with-self-normalization
 */
float fast_atan2f(float y, float x){
  const float coeff_1 = M_PI/4;
  const float coeff_2 = 3*M_PI/4;
  float abs_y = fabs(y)+1e-10; // kludge to prevent 0/0 condition
  float r, angle;
  if (x>=0){
    r = (x - abs_y) / (x + abs_y);
    angle = coeff_1 - coeff_1 * r;
  }else{
    r = (x + abs_y) / (abs_y - x);
    angle = coeff_2 - coeff_1 * r;
  }
  if(y < 0)
    return(-angle); // negate if in quad III or IV
  else
    return(angle);
}

/* static const float* log_table = fast_log_table; */
/* static uint32_t log_precision = fast_log_precision; */
/* static const uint32_t* pow_table = fast_pow_table; */
/* static uint32_t pow_precision = fast_pow_precision; */

/*static const float* log_table;
static uint32_t log_precision;
static const uint32_t* pow_table;
static uint32_t pow_precision;*/

#define M_LOG210 3.32192809488736

float fast_fmodf(float x, float y) {
  float a = x/y;
  return (a-(int)a)*y;
}

uint32_t fast_log2i(uint32_t x){
  return 31 - __builtin_clz (x); /* clz returns the number of leading 0's */
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

// --- Request Macros ---
// Ensure we don't collide if math.h was included previously
// and clean up existing defines if necessary.

#ifdef log2
#undef log2
#endif

// Power
#define pow(x, y) fast_powf(x, y)
#define powf(x, y) fast_powf(x, y)

// Exp
#define exp(x) fast_expf(x)
#define expf(x) fast_expf(x)
#define exp2(x) fast_exp2f(x)
#define exp2f(x) fast_exp2f(x)
#define exp10(x) fast_exp10f(x)
#define exp10f(x) fast_exp10f(x)

// Log
#define log(x) fast_logf(x)
#define logf(x) fast_logf(x)
#define log2(x) fast_log2f(x)
#define log2f(x) fast_log2f(x)
#define log10(x) fast_log10f(x)
#define log10f(x) fast_log10f(x)

#endif // __basicmaths_h__
