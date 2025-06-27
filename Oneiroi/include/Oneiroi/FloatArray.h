#ifndef __FloatArray_h__
#define __FloatArray_h__

#include <cstddef>
#include "SimpleArray.h"
#include "basicmaths.h"
#include <string.h>

/**
 * This class contains useful methods for manipulating arrays of floats.
 * It also provides a convenient handle to the array pointer and the size of the array.
 * FloatArray objects can be passed by value without copying the contents of the array.
 */
class FloatArray : public SimpleArray<float> {
public:
  FloatArray(){}
  FloatArray(float* data, size_t size) :
    SimpleArray(data, size) {}

  /**
   * Set all the values in the array.
   * Sets all the elements of the array to **value**.
   * @param[in] value all the elements are set to this value.
  */
  void setAll(float value);

  /**
   * Clear the array.
   * Set all the values in the array to 0.
  */
  void clear(){
    setAll(0);
  }
  
  
  /**
   * Get the minimum value in the array and its index
   * @param[out] value will be set to the minimum value after the call
   * @param[out] index will be set to the index of the minimum value after the call
   * 
   */
  void getMin(float* value, int* index);
  
  /**
   * Get the maximum value in the array and its index
   * @param[out] value will be set to the maximum value after the call
   * @param[out] index will be set to the index of the maximum value after the call
  */
  void getMax(float* value, int* index);
  
  /**
   * Get the minimum value in the array
   * @return the minimum value contained in the array
  */
  float getMinValue();
  
  /**
   * Get the maximum value in the array
   * @return the maximum value contained in the array
   */
  float getMaxValue();
  
  /**
   * Get the index of the minimum value in the array
   * @return the mimimum value contained in the array
   */
  int getMinIndex();
  
  /**
   * Get the index of the maximum value in the array
   * @return the maximum value contained in the array
   */
  int getMaxIndex();
  
  /**
   * Absolute value of the array.
   * Stores the absolute value of the elements in the array into destination.
   * @param[out] destination the destination array.
  */
  void rectify(FloatArray& destination);
  
  /**
   * Absolute value of the array, in-place version.
   * Sets each element in the array to its absolute value.
   */
  void rectify(){
    rectify(*this);
  }
  
  /**
   * Reverse the array
   * Copies the elements of the array in reversed order into destination.
   * @param[out] destination the destination array.
  */
  void reverse(FloatArray& destination);
  
  /**
   * Reverse the array.
   * Reverses the order of the elements in the array.
  */
  void reverse(); //in place
  
  /**
   * Reciprocal of the array.
   * Stores the reciprocal of the elements in the array into destination.
   * @param[out] destination the destination array.
  */
  void reciprocal(FloatArray& destination);
  
  /**
   * Reciprocal of the array, in-place version.
   * Sets each element in the array to its reciprocal.
  */
  void reciprocal(){
    reciprocal(*this);
  }
  
  /**
   * Negate the array.
   * Stores the opposite of the elements in the array into destination.
   * @param[out] destination the destination array.
   * @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
   */
  void negate(FloatArray& destination);
  
  /**
   * Negate the array.
   * Sets each element in the array to its opposite.
  */
  void negate(){
    negate(*this);
  }
  
  /**
   * Random values
   * Fills the array with random values in the range [-1, 1)
   */
  void noise();
  
  /**
   * Random values in range.
   * Fills the array with random values in the range [**min**, **max**)
   * @param min minimum value in the range
   * @param max maximum value in the range 
   */
  void noise(float min, float max);
  
  /**
   * Root mean square value of the array.
   * Gets the root mean square of the values in the array.
  */
  float getRms();
  
  /**
   * Mean of the array.
   * Gets the mean (or average) of the values in the array.
  */
  float getMean();
  
  /**
   * Sum of the array.
   * Gets the sum of the values in the array.
  */
  float getSum();
  
  /**
   * Power of the array.
   * Gets the power of the values in the array.
  */
  float getPower();
  
  /**
   * Standard deviation of the array.
   * Gets the standard deviation of the values in the array.
  */
  float getStandardDeviation();
  
  /**
   * Variance of the array.
   * Gets the variance of the values in the array.
  */
  float getVariance();

  /**
   * Clips the elements in the array in the range [-1, 1].
  */
  void clip();
  
  /**
   * Clips the elements in the array in the range [-**range**, **range**].
   * @param range clipping value.
  */
  void clip(float range);
  
  /**
   * Clips the elements in the array in the range [**min**, **max**].
   * @param min minimum value
   * @param max maximum value
  */
  void clip(float min, float max);

  /**
   * Applies a cubic soft-clip algorithm to all elements in the array which limits them to the range [-1, 1]
   * @param[out] destination the destination array
   */
  void softclip(FloatArray destination);

  /**
   * Applies a cubic soft-clip algorithm to all elements in the array which limits them to the range [-1, 1]
   * In-place version.
   */
  void softclip(){
    softclip(*this);
  }  
  
  /**
   * Element-wise sum between arrays.
   * Sets each element in **destination** to the sum of the corresponding element of the array and **operand2**
   * @param[in] operand2 second operand for the sum
   * @param[out] destination the destination array
  */
  void add(FloatArray operand2, FloatArray destination);
  
  /**
   * Element-wise sum between arrays.
   * Adds each element of **operand2** to the corresponding element in the array.
   * @param operand2 second operand for the sum
  */
  void add(FloatArray operand2); //in-place
  
  /**
   * Array-scalar addition.
   * Adds **scalar** to each value in the array and put the result in **destination**
   * @param[in] scalar value to be added to the array
   * @param[out] destination the destination array
   */
  void add(float scalar, FloatArray destination);

  /**
   * In-place array-scalar addition.
   * Adds **scalar** to each value in the array
   * @param[in] scalar value to be added to the array
  */
  void add(float scalar);

  /**
   * Element-wise difference between arrays.
   * Sets each element in **destination** to the difference between the corresponding element of the array and **operand2**
   * @param[in] operand2 second operand for the subtraction
   * @param[out] destination the destination array
  */
  void subtract(FloatArray operand2, FloatArray destination);
  
  
  /**
   * Element-wise difference between arrays.
   * Subtracts from each element of the array the corresponding element in **operand2**.
   * @param[in] operand2 second operand for the subtraction
  */
  void subtract(FloatArray operand2); //in-place
  
  /**
   * Array-scalar subtraction.
   * Subtracts **scalar** from the values in the array.
   * @param scalar to be subtracted from the array
  */
  void subtract(float scalar);
  
/**
   * Element-wise multiplication between arrays.
   * Sets each element in **destination** to the product of the corresponding element of the array and **operand2**
   * @param[in] operand2 second operand for the product
   * @param[out] destination the destination array
  */
  void multiply(FloatArray operand2, FloatArray destination);
  
   /**
   * Element-wise multiplication between arrays.
   * Multiplies each element in the array by the corresponding element in **operand2**.
   * @param operand2 second operand for the sum
  */
  void multiply(FloatArray operand2); //in-place
  
  /**
   * Array-scalar multiplication.
   * Multiplies the values in the array by **scalar**.
   * @param scalar to be multiplied with the array elements
  */
  void multiply(float scalar);
  
  /**
   * Array-scalar multiplication.
   * Multiplies the values in the array by **scalar**.
   * @param scalar to be subtracted from the array
   * @param destination the destination array
  */
  void multiply(float scalar, FloatArray destination);

  /**
   * Convolution between arrays.
   * Sets **destination** to the result of the convolution between the array and **operand2**
   * @param[in] operand2 the second operand for the convolution
   * @param[out] destination array. It must have a minimum size of this+other-1.
   * @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
   */
  void convolve(FloatArray operand2, FloatArray destination);
  
  /** 
   * Partial convolution between arrays.
   * Perform partial convolution: start at **offset** and compute **samples** values.
   * @param[in] operand2 the second operand for the convolution.
   * @param[out] destination the destination array.
   * @param[in] offset first output sample to compute
   * @param[in] samples number of samples to compute
   * @remarks **destination[n]** is left unchanged for n<offset and the result is stored from destination[offset] onwards
   * that is, in the same position where they would be if a full convolution was performed.
   * @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
  */
  void convolve(FloatArray operand2, FloatArray destination, int offset, size_t samples);
  
  /** 
   * Correlation between arrays.
   * Sets **destination** to the correlation of the array and **operand2**.
   * @param[in] operand2 the second operand for the correlation
   * @param[out] destination the destination array. It must have a minimum size of 2*max(srcALen, srcBLen)-1
   * @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
  */
  void correlate(FloatArray operand2, FloatArray destination);
  
  /**
   * Correlation between arrays.
   * Sets **destination** to the correlation of *this* array and **operand2**.
   * @param[in] operand2 the second operand for the correlation
   * @param[out] destination array. It must have a minimum size of 2*max(srcALen, srcBLen)-1
   * @remarks It is the same as correlate(), but destination must have been initialized to 0 in advance. 
  */
  void correlateInitialized(FloatArray operand2, FloatArray destination);

  /**
   * Convert gains to decibel values: dB = log10(gain)*20
   * Gain 0.5 = -6dB, 1.0 = 0dB, 2.0 = +6dB
   */
  void gainToDecibel(FloatArray destination);

  /**
   * Convert decibel to gains values: gain = 10^(dB/20)
   * -6dB = 0.5, 0dB = 1.0, +6dB = 2.0
   */  
  void decibelToGain(FloatArray destination);
  
  /**
   * A subset of the array.
   * Returns a array that points to subset of the memory used by the original array.
   * @param[in] offset the first element of the subset.
   * @param[in] length the number of elments in the new FloatArray.
   * @return the newly created FloatArray.
   * @remarks no memory is allocated by this method. The memory is still shared with the original array.
   * The memory should not be de-allocated elsewhere (e.g.: by calling FloatArray::destroy() on the original FloatArray) 
   * as long as the FloatArray returned by this method is still in use.
   * @remarks Calling FloatArray::destroy() on a FloatArray instance created with this method might cause an exception.
  */
  FloatArray subArray(int offset, size_t length);
  /**
   * Create a linear ramp from one value to another.
   * Interpolates all samples in the FloatArray between the endpoints **from** to **to**.
   */
  void ramp(float from, float to);

  /**
   * Scale all values along a linear ramp from one value to another.
   */  
  void scale(float from, float to, FloatArray destination);

  /**
   * In-place scale.
   */  
  void scale(float from, float to){
    scale(from, to, *this);
  }

  /**
   * Apply tanh to each element in the array
   */
  void tanh(FloatArray destination);

  /**
   * In-place tanh
   */
  void tanh(){
    tanh(*this);
  }

  /**
   * Creates a new FloatArray.
   * Allocates size*sizeof(float) bytes of memory and returns a FloatArray that points to it.
   * @param size the size of the new FloatArray.
   * @return a FloatArray which **data** point to the newly allocated memory and **size** is initialized to the proper value.
   * @remarks a FloatArray created with this method has to be destroyed invoking the FloatArray::destroy() method.
  */
  static FloatArray create(int size);
  
  /**
   * Destroys a FloatArray created with the create() method.
   * @param array the FloatArray to be destroyed.
   * @remarks the FloatArray object passed as an argument should not be used again after invoking this method.
   * @remarks a FloatArray object that has not been created by the FloatArray::create() method might cause an exception if passed as an argument to this method.
  */
  static void destroy(FloatArray array);
};

void FloatArray::getMin(float* value, int* index){
/// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
#ifdef ARM_CORTEX
  uint32_t idx;
  arm_min_f32(data, size, value, &idx);
  *index = (int)idx;
#else
  *value=data[0];
  *index=0;
  for(size_t n=1; n<size; n++){
    float currentValue=data[n];
    if(currentValue<*value){
      *value=currentValue;
      *index=n;
    }
  }
#endif
}

float FloatArray::getMinValue(){
  float value;
  int index;
  /// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
  getMin(&value, &index);
  return value;
}

int FloatArray::getMinIndex(){
  float value;
  int index;
  /// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
  getMin(&value, &index);
  return index;
}

void FloatArray::getMax(float* value, int* index){
  
/// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
#ifdef ARM_CORTEX 
  uint32_t idx;
  arm_max_f32(data, size, value, &idx);
  *index = (int)idx;
#else
  *value=data[0];
  *index=0;
  for(size_t n=1; n<size; n++){
    float currentValue=data[n];
    if(currentValue>*value){
      *value=currentValue;
      *index=n;
    }
  }
#endif
}

float FloatArray::getMaxValue(){
  float value;
  int index;
  /// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
  getMax(&value, &index);
  return value;
}

int FloatArray::getMaxIndex(){
  float value;
  int index;
  /// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
  getMax(&value, &index);
  return index;
}

void FloatArray::rectify(FloatArray& destination){ //this is actually "copy data with rectifify"
/// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
#ifdef ARM_CORTEX   
  arm_abs_f32(data, destination.getData(), size);
#else
  size_t minSize= min(size,destination.getSize()); //TODO: shall we take this out and allow it to segfault?
  for(size_t n=0; n<minSize; n++){
    destination[n] = fabsf(data[n]);
  }
#endif  
}

void FloatArray::reverse(FloatArray& destination){ //this is actually "copy data with reverse"
  if(destination==*this){ //make sure it is not called "in-place"
    reverse();
    return;
  }
  for(size_t n=0; n<size; n++){
    destination[n]=data[size-n-1];
  }
}

void FloatArray::reverse(){//in place
  for(size_t n=0; n<size/2; n++){
    float temp=data[n];
    data[n]=data[size-n-1];
    data[size-n-1]=temp;
  }
}

void FloatArray::reciprocal(FloatArray& destination){
  float* data = getData();
  for(size_t n=0; n<getSize(); n++)
    destination[n] = 1.0f/data[n];
}

float FloatArray::getRms(){
  float result;
/// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
#ifdef ARM_CORTEX  
  arm_rms_f32 (data, size, &result);
#else
  result=0;
  float *pSrc= data;
  for(size_t n=0; n<size; n++){
    result += pSrc[n]*pSrc[n];
  }
  result=sqrtf(result/size);
#endif
  return result;
}

float FloatArray::getSum(){
  float result = 0;
  for(size_t n=0; n<size; n++)
    result += data[n];
  return result;
} 

float FloatArray::getMean(){
  float result;
/// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
#ifdef ARM_CORTEX  
  arm_mean_f32 (data, size, &result);
#else
  result=0;
  for(size_t n=0; n<size; n++){
    result+=data[n];
  }
  result=result/size;
#endif
  return result;
}

float FloatArray::getPower(){
  float result;
/// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
#ifdef ARM_CORTEX  
  arm_power_f32 (data, size, &result);
#else
  result=0;
  float *pSrc = data;
  for(size_t n=0; n<size; n++){
    result += pSrc[n]*pSrc[n];
  }
#endif
  return result;
}

float FloatArray::getStandardDeviation(){
  float result;
/// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
#ifdef ARM_CORTEX  
  arm_std_f32 (data, size, &result);
#else
  result=sqrtf(getVariance());
#endif
  return result;
}

float FloatArray::getVariance(){
  float result;
/// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
#ifdef ARM_CORTEX  
  arm_var_f32(data, size, &result);
#else
  float sumOfSquares=getPower();
  float sum=0;
  for(size_t n=0; n<size; n++){
    sum+=data[n];
  }
  result=(sumOfSquares - sum*sum/size) / (size - 1);
#endif
  return result;
}

void FloatArray::clip(){
  clip(1);
}

void FloatArray::clip(float max){
  for(size_t n=0; n<size; n++){
    if(data[n]>max)
      data[n]=max;
    else if(data[n]<-max)
      data[n]=-max;
  }
}

void FloatArray::clip(float min, float max){
  for(size_t n=0; n<size; n++){
    if(data[n]>max)
      data[n]=max;
    else if(data[n]<min)
      data[n]=min;
  }
}

FloatArray FloatArray::subArray(int offset, size_t length){
  return FloatArray(data+offset, length);
}

void FloatArray::setAll(float value){
/// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
#ifdef ARM_CORTEX
  arm_fill_f32(value, data, size);
#else
  for(size_t n=0; n<size; n++){
    data[n]=value;
  }
#endif /* ARM_CORTEX */
}

void FloatArray::add(FloatArray operand2, FloatArray destination){ //allows in-place
/// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
#ifdef ARM_CORTEX
  /* despite not explicitely documented in the CMSIS documentation,
      this has been tested to behave properly even when pSrcA==pDst
      void 	arm_add_f32 (float32_t *pSrcA, float32_t *pSrcB, float32_t *pDst, uint32_t blockSize)
  */
  arm_add_f32(data, operand2.data, destination.data, size);
#else
  for(size_t n=0; n<size; n++){
    destination[n]=data[n]+operand2[n];
  }
#endif /* ARM_CORTEX */
}

void FloatArray::add(FloatArray operand2){ //in-place
/// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
  add(operand2, *this);
}

void FloatArray::add(float scalar){
  for(size_t n=0; n<size; n++){
    data[n] += scalar;
  } 
}

void FloatArray::add(float scalar, FloatArray destination){
  for(size_t n=0; n<size; n++)
    destination[n] = data[n]+scalar;
}

void FloatArray::subtract(FloatArray operand2, FloatArray destination){ //allows in-place
  /// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
#ifdef ARM_CORTEX
  /* despite not explicitely documented in the CMSIS documentation,
      this has been tested to behave properly even when pSrcA==pDst
      void 	arm_sub_f32 (float32_t *pSrcA, float32_t *pSrcB, float32_t *pDst, uint32_t blockSize)
  */
  arm_sub_f32(data, operand2.data, destination.data, size);
  #else
  for(size_t n=0; n<size; n++){
    destination[n]=data[n]-operand2[n];
  }
  #endif /* ARM_CORTEX */
}

void FloatArray::subtract(FloatArray operand2){ //in-place
/// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
  subtract(operand2, *this);
}

void FloatArray::subtract(float scalar){
  for(size_t n=0; n<size; n++){
    data[n]-=scalar;
  } 
}

void FloatArray::multiply(FloatArray operand2, FloatArray destination){ //allows in-place
   /// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
#ifdef ARM_CORTEX
  /* despite not explicitely documented in the CMSIS documentation,
      this has been tested to behave properly even when pSrcA==pDst
      void 	arm_mult_f32 (float32_t *pSrcA, float32_t *pSrcB, float32_t *pDst, uint32_t blockSize)
  */
    arm_mult_f32(data, operand2.data, destination, size);
  #else
  for(size_t n=0; n<size; n++){
    destination[n]=data[n]*operand2[n];
  }

  #endif /* ARM_CORTEX */
}

void FloatArray::multiply(FloatArray operand2){ //in-place
  /// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
  multiply(operand2, *this);
}

void FloatArray::multiply(float scalar){
#ifdef ARM_CORTEX
  arm_scale_f32(data, scalar, data, size);
#else
  for(size_t n=0; n<size; n++)
    data[n]*=scalar;
#endif
}

void FloatArray::multiply(float scalar, FloatArray destination){
#ifdef ARM_CORTEX
  arm_scale_f32(data, scalar, destination, size);
#else
  for(size_t n=0; n<size; n++)
    destination[n] = data[n] * scalar;
#endif
}

void FloatArray::negate(FloatArray& destination){//allows in-place
#ifdef ARM_CORTEX
  arm_negate_f32(data, destination.getData(), size); 
#else
  for(size_t n=0; n<size; n++){
    destination[n]=-data[n];
  }
#endif /* ARM_CORTEX */
}

void FloatArray::noise(){
  noise(-1, 1);
}

void FloatArray::noise(float min, float max){
  float amplitude = fabsf(max-min);
  float offset = min;
  for(size_t n=0; n<size; n++)
    data[n] = randf() * amplitude + offset;
}


void FloatArray::convolve(FloatArray operand2, FloatArray destination){
#ifdef ARM_CORTEX
  arm_conv_f32(data, size, operand2.data, operand2.size, destination);
#else
  size_t size2 = operand2.getSize();
  for(size_t n=0; n<size+size2-1; n++){
    size_t n1 = n;
    destination[n] = 0;
    for(size_t k=0; k<size2; k++){
      if(n1>=0 && n1<size)
        destination[n] += data[n1]*operand2[k];
      n1--;
    }
  }
#endif /* ARM_CORTEX */
}

void FloatArray::convolve(FloatArray operand2, FloatArray destination, int offset, size_t samples){
/// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
#ifdef ARM_CORTEX
  //TODO: I suspect a bug in arm_conv_partial_f32
  //it seems that destination[n] is left unchanged for n<offset
  //and the result is actually stored from destination[offset] onwards
  //that is, in the same position where they would be if a full convolution was performed.
  //This requires (destination.size >= size + operand2.size -1). Ideally you would want destination to be smaller
  arm_conv_partial_f32(data, size, operand2.data, operand2.size, destination.getData(), offset, samples);
#else
  //this implementations reproduces the (buggy?) behaviour of arm_conv_partial (see comment above and inline comments below)
  /*
  This implementation is just a copy/paste/edit from the overloaded method
  */
  size_t size2=operand2.getSize();
  for (size_t n=offset; n<offset+samples; n++){
    size_t n1=n;
    destination[n] =0; //this should be [n-offset]
    for(size_t k=0; k<size2; k++){
      if(n1>=0 && n1<size)
        destination[n]+=data[n1]*operand2[k];//this should be destination[n-offset]
      n1--;
    }
  }
#endif /* ARM_CORTEX */
}

void FloatArray::correlate(FloatArray operand2, FloatArray destination){ 
  destination.setAll(0);
  /// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
  correlateInitialized(operand2, destination);
}

void FloatArray::correlateInitialized(FloatArray operand2, FloatArray destination){
/// @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
#ifdef ARM_CORTEX
  arm_correlate_f32(data, size, operand2.data, operand2.size, destination);
#else
  //correlation is the same as a convolution where one of the signals is flipped in time
  //so we flip in time operand2 
  operand2.reverse();
  //and convolve it with fa to obtain the correlation
  convolve(operand2, destination);
  //and we flip back operand2, so that the input is not modified
  operand2.reverse();
#endif /* ARM_CORTEX */  
}

void FloatArray::gainToDecibel(FloatArray destination){
  for(size_t i=0; i<size; i++)
    destination[i] = log10f(data[i])*20.0;
}

void FloatArray::decibelToGain(FloatArray destination){
  for(size_t i=0; i<size; i++)
    destination[i] = exp10f(data[i]*0.05);
}

void FloatArray::ramp(float from, float to){
  float step = (to-from)/size;
  for(size_t i=0; i<size; i++){
    data[i] = from;
    from += step;
  }
}

void FloatArray::scale(float from, float to, FloatArray destination){
  float step = (to-from)/size;
  for(size_t i=0; i<size; i++){
    data[i] *= from;
    from += step;
  }  
}

/*
 * Third-order static soft-clipping function.
 * ref:  T. Araya and A. Suyama, “Sound effector capable of
 * imparting plural sound effects like distortion and other
 * effects,” US Patent 5,570,424, 29 Oct. 1996.
 */
void FloatArray::softclip(FloatArray destination){
  for(size_t i=0; i<size; i++){
    float x = data[i];
    destination[i] = clamp((3*x/2)*(1-x*x/3), -1.0f, 1.0f);
  }
}
/*
void FloatArray::tanh(FloatArray destination){
  for(size_t i=0; i<size; i++)
    destination[i] = tanhf(data[i]);
}*/

FloatArray FloatArray::create(int size){
  FloatArray fa(new float[size], size);
  fa.clear();
  return fa;
}

void FloatArray::destroy(FloatArray array){
  //delete[] array.data;
}

#endif // __FloatArray_h__
