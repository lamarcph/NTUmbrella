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

   #define exp10(x) powf(10.0f, x)
   #define exp10f(x) powf(10.0f, x)

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

#ifdef __FAST_MATH__ /* set by gcc option -ffast-math */

// fast lookup-based exponentials
#define pow(x, y) fast_powf(x, y)
#define powf(x, y) fast_powf(x, y)
#define exp(x) fast_expf(x)
#define expf(x) fast_expf(x)
#define exp2(x) fast_exp2f(x)
#define exp2f(x) fast_exp2f(x)
#define exp10(x) fast_exp10f(x)
#define exp10f(x) fast_exp10f(x)

// fast lookup-based logarithmics
#ifdef log2
#undef log2 /* defined in math.h */
#endif
#define log(x) fast_logf(x)
#define logf(x) fast_logf(x)
#define log2(x) fast_log2f(x)
#define log2f(x) fast_log2f(x)
#define log10(x) fast_log10f(x)
#define log10f(x) fast_log10f(x)

#else /* __FAST_MATH__ */

#define exp10(x) powf(10, x)
#define exp10f(x) powf(10, x)

#endif /* __FAST_MATH__ */

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
}

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

static const float* log_table;
static uint32_t log_precision;
static const uint32_t* pow_table;
static uint32_t pow_precision;

#define M_LOG210 3.32192809488736

float fast_fmodf(float x, float y) {
  float a = x/y;
  return (a-(int)a)*y;
}

uint32_t fast_log2i(uint32_t x){
  return 31 - __builtin_clz (x); /* clz returns the number of leading 0's */
}


#endif // __basicmaths_h__
